"""Inspect ONNX graphs and copy models with explicit DML decoding sidecars.

Requires: pip install onnx numpy
Optional runtime verification: pip install onnxruntime-directml
Original models are never rewritten. Missing raw-head anchors are reported,
never replaced with a different model's anchors.
"""
import argparse
import ast
import hashlib
import json
import shutil
from pathlib import Path

import numpy as np
import onnx


def shape(value):
    return [d.dim_value if d.HasField("dim_value") else d.dim_param
            for d in value.type.tensor_type.shape.dim]


def class_names(model):
    raw = next((p.value for p in model.metadata_props if p.key == "names"), "")
    if not raw:
        return {}
    value = ast.literal_eval(raw)
    names = dict(enumerate(value)) if isinstance(value, list) else value
    if not isinstance(names, dict) or sorted(names) != list(range(len(names))):
        raise ValueError("names metadata must have contiguous integer class IDs")
    return names


def inspect_model(path):
    model = onnx.load(path, load_external_data=False)
    onnx.checker.check_model(str(path))
    names = class_names(model)
    inputs = [{"name": v.name, "shape": shape(v)} for v in model.graph.input]
    outputs = [{"name": v.name, "shape": shape(v)} for v in model.graph.output]
    if len(inputs) != 1 or len(inputs[0]["shape"]) != 4 or inputs[0]["shape"][1] != 3:
        raise ValueError("Expected one NCHW RGB input")
    if any(v.type.tensor_type.elem_type != onnx.TensorProto.FLOAT
           for v in [*model.graph.input, *model.graph.output]):
        raise ValueError("Expected FP32 input/output")
    profile = {"source_sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
    warnings = []
    decoded = next((v for v in outputs if len(v["shape"]) == 3), None)
    if decoded:
        _, rows, cols = decoded["shape"]
        profile["output"] = decoded["name"]
        feature = isinstance(rows, int) and (not isinstance(cols, int) or rows < cols)
        extent = rows if feature else cols
        profile["layout"] = "feature" if feature else "prediction"
        objectness = extent == len(names) + 5 if names else not feature
        profile["objectness"] = int(objectness)
        profile["classes"] = len(names) if names else extent - (5 if objectness else 4)
        if extent != profile["classes"] + (5 if objectness else 4):
            raise ValueError("Class metadata does not match output shape")
        # Identify the actual YOLOX head, not the file name or six-column shape.
        initializers = {t.name for t in model.graph.initializer}
        yolox = all(any(f"head.{kind}_preds." in n for n in initializers)
                    for kind in ("reg", "obj", "cls"))
        if yolox:
            producers = {out: node for node in model.graph.node for out in node.output}
            node = producers[decoded["name"]]
            if node.op_type != "Transpose" or producers[node.input[0]].op_type != "Concat":
                raise ValueError("YOLOX output is not the verified raw export layout")
            if any(n.op_type == "Exp" for n in model.graph.node):
                raise ValueError("YOLOX graph includes coordinate decoding; inspect before preparing")
            profile["layout"] = "yolox_raw"
            profile["strides"] = "8,16,32"
            h, w = inputs[0]["shape"][2:]
            if not isinstance(h, int) or not isinstance(w, int) or sum((h//s)*(w//s) for s in (8,16,32)) != rows:
                raise ValueError("YOLOX candidate count does not match strides 8,16,32")
            warnings.append("YOLOX raw box coordinates require grid/exp decoding")
        elif not feature and extent == 6 and not names:
            raise ValueError("Ambiguous six-column output without class metadata: copy the model and supply an explicit layout/classes sidecar after verifying its export format")
    elif all(len(v["shape"]) == 5 for v in outputs):
        classes = outputs[0]["shape"][-1] - 5
        if any(v["shape"][-1] != classes + 5 for v in outputs):
            raise ValueError("Raw head class dimensions disagree")
        profile.update(layout="yolov5_raw", classes=classes, objectness=1)
        warnings.append("BLOCKED: original pixel anchors are absent; supply anchors.<output-name> in sidecar")
    else:
        raise ValueError("Unsupported output contract")
    profile["stride"] = 32
    profile["color_order"] = "bgr" if profile["layout"] == "yolox_raw" else "rgb"
    profile["input_scale"] = 1.0 if profile["layout"] == "yolox_raw" else 1.0 / 255.0
    profile["names"] = json.dumps(names, ensure_ascii=False)
    # Names describe labels; they do not determine friend/enemy roles.
    profile["class_player"] = -1
    profile["class_head"] = -1
    profile["class_roles"] = ",".join(["ignore"] * profile["classes"])
    warnings.append("Target roles unassigned: choose enemy/friendly body/head roles in the Overlay")
    return profile, {"file": path.name, "inputs": inputs, "outputs": outputs,
                     "profile": profile, "warnings": warnings}


def verify_runtime(path, record):
    import onnxruntime as ort
    if "DmlExecutionProvider" not in ort.get_available_providers():
        raise RuntimeError("DmlExecutionProvider is unavailable")
    opts = ort.SessionOptions()
    opts.enable_mem_pattern = False
    opts.execution_mode = ort.ExecutionMode.ORT_SEQUENTIAL
    opts.log_severity_level = 3
    session = ort.InferenceSession(str(path), sess_options=opts,
                                   providers=["DmlExecutionProvider", "CPUExecutionProvider"])
    dims = [d if isinstance(d, int) and d > 0 else (1 if i == 0 else 320)
            for i, d in enumerate(session.get_inputs()[0].shape)]
    # Deterministic nonconstant input exercises the graph, but is not an accuracy test.
    data = np.random.default_rng(42).random(dims, dtype=np.float32)
    data *= 255 * record["profile"]["input_scale"]
    result = session.run(None, {session.get_inputs()[0].name: data})
    if any(not np.isfinite(v).all() for v in result):
        raise RuntimeError("Inference produced non-finite values")
    record["runtime"] = {"providers": session.get_providers(), "input_shape": dims,
                         "output_shapes": [list(v.shape) for v in result], "finite": True,
                         "ort_version": ort.__version__}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--destination", type=Path, required=True)
    parser.add_argument("--verify-dml", action="store_true")
    args = parser.parse_args()
    if args.source.resolve() == args.destination.resolve():
        parser.error("Destination must differ from source; originals are preserved")
    args.destination.mkdir(parents=True, exist_ok=True)
    records = []
    for source in sorted(args.source.glob("*.onnx")):
        try:
            profile, record = inspect_model(source)
            dest = args.destination / source.name
            if dest.exists() and hashlib.sha256(dest.read_bytes()).hexdigest() != profile["source_sha256"]:
                raise ValueError("Destination contains a different model; refusing to overwrite")
            if not dest.exists():
                shutil.copy2(source, dest)
            sidecar = dest.with_suffix(dest.suffix + ".ini")
            if not sidecar.exists():
                sidecar.write_text("# Generated decoding contract; edit class mapping/anchors as needed.\n" +
                                   "\n".join(f"{k}={v}" for k, v in profile.items()) + "\n", encoding="utf-8")
            else:
                record["warnings"].append("Existing sidecar preserved; report shows inferred profile")
            if args.verify_dml:
                verify_runtime(dest, record)
            records.append(record)
            print(json.dumps({"file": source.name, "warnings": record["warnings"],
                              "runtime": record.get("runtime")}, ensure_ascii=True), flush=True)
        except Exception as exc:
            record = {"file": source.name, "error": str(exc)}
            records.append(record)
            print(json.dumps(record, ensure_ascii=True), flush=True)
    (args.destination / "model-audit.json").write_text(json.dumps(records, ensure_ascii=False, indent=2), encoding="utf-8")
    return int(not records or any("error" in r for r in records))


if __name__ == "__main__":
    raise SystemExit(main())
