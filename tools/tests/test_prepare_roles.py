"""Preparation must not infer targeting roles from arbitrary training labels."""
import sys
import tempfile
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import onnx
from onnx import helper, TensorProto
from prepare_onnx_models import inspect_model

with tempfile.TemporaryDirectory() as folder:
    path = Path(folder) / "roles.onnx"
    for labels in (["enemy", "head", "friend"], ["tbody", "ctbody", "unknown##label"], ["a", "b", "c"]):
        output = helper.make_tensor("value", TensorProto.FLOAT, [1, 7, 10], [0.0] * 70)
        graph = helper.make_graph([helper.make_node("Constant", [], ["output"], value=output)], "roles",
            [helper.make_tensor_value_info("images", TensorProto.FLOAT, [1, 3, 320, 320])],
            [helper.make_tensor_value_info("output", TensorProto.FLOAT, [1, 7, 10])])
        model = helper.make_model(graph)
        helper.set_model_props(model, {"names": repr(labels)})
        onnx.save(model, path)
        profile, report = inspect_model(path)
        assert profile["class_player"] == -1 and profile["class_head"] == -1, "Names must not enable target classes"
        assert profile["class_roles"] == "ignore,ignore,ignore", "New models require manual role selection"
        assert labels[2] in profile["names"], "Original class names must remain available"
    # No metadata distinguishes YOLO predictions from XYXY/class-id rows here.
    output = helper.make_tensor("value", TensorProto.FLOAT, [1, 10, 6], [0.0] * 60)
    graph = helper.make_graph([helper.make_node("Constant", [], ["output"], value=output)], "ambiguous",
        [helper.make_tensor_value_info("images", TensorProto.FLOAT, [1, 3, 320, 320])],
        [helper.make_tensor_value_info("output", TensorProto.FLOAT, [1, 10, 6])])
    onnx.save(helper.make_model(graph), path)
    try:
        inspect_model(path)
    except ValueError as error:
        assert "Ambiguous six-column" in str(error), str(error)
    else:
        raise AssertionError("Preparation must not guess a decoder for ambiguous six-column outputs")
print("Model preparation manual roles and ambiguous layout tests: passed")
