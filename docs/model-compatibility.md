# ONNX model compatibility (DirectML)

The DML detector supports explicit feature-major `[B,4+C,N]`, prediction-major
`[B,N,5+C]`, XYXY post-NMS, raw YOLOX and raw YOLOv5 heads. It validates FP32
NCHW input/output contracts and uses static model dimensions even when the
capture size changes. Dynamic image dimensions must be stride-aligned.

## Prepare a model collection

Install `onnx` and `numpy` into a Python environment. For optional GPU graph
checks also install `onnxruntime-directml`.

```powershell
python tools/prepare_onnx_models.py F:\sunone_aimbot_2-main\build\Release\models --destination build/dml/Release/models --verify-dml
```

The tool copies original ONNX files without modifying their graphs and creates
`<filename>.onnx.ini` next to each copy. Existing sidecars are preserved, so
re-running preparation does not erase manual class mappings or anchors.
`model-audit.json` records graph shapes, metadata, warnings and optional runtime
checks. The SHA-256 in a sidecar records source provenance; the application does
not use it to authenticate a model.

For example, `PUBGV8S_640.onnx.ini` contains:

```ini
layout=feature
output=output0
classes=2
objectness=0
stride=32
class_player=1
class_head=0
```

Supported `layout` values: `feature`, `prediction`, `xyxy`, `yolox_raw`,
`yolov5_raw`. `classes` is the total number of trained classes, independent of
which class is selected for use. For YOLOX, `strides=8,16,32` specifies the grid
ordering; box coordinates are decoded with grid offsets and exp-scaled sizes.
Objectness/class probabilities from this export are already sigmoid-transformed.

`color_order=rgb|bgr` and `input_scale` control channel order and pixel scaling.
Preparation uses RGB with 1/255 for YOLOv5/v8-style graphs and the standard
YOLOX BGR, unscaled 0..255 convention for the identified raw YOLOX graph.
These are exporter conventions, not proof of the unknown training pipeline;
override them if the model's training preprocessing differs. Frames are resized
directly to the model dimensions; the supplied collection and normal capture
use square images. Non-square images requiring letterbox preprocessing need a
matching preprocessing and inverse-coordinate transform before accuracy claims.

The `names` metadata can be a Python dictionary or list. It is inspected as
data, never evaluated as code. Unambiguous decoded YOLO models can also load
without sidecars. An ambiguous six-column tensor without metadata requires an
explicit sidecar instead of guessing from its output values. The preparation tool
also rejects such a tensor unless the graph matches the checked raw YOLOX export
pattern; it does not generate a speculative single-class YOLO sidecar.

## Class mappings

### Overlay selection (DirectML build)

Open the Overlay with **Home**, then select **AI Model**. The **ONNX information**
section shows the loaded model, actual provider, FP32 input/output tensor shapes,
class IDs/names and expandable original custom metadata. Symbolic dimensions are
shown as dynamic symbols. Decoder/preprocessing settings are identified separately
because these settings do not establish the original training pipeline.

In **Target classes**, each numeric class ID and original name has a role selector:
**Ignore**, **Enemy body**, **Enemy head**, **Friendly body**, or **Friendly head**.
Names can be anything (including missing names); no name is hardcoded to a role.
Multiple class IDs can share a role. The held enemy/friendly hotkey selects which team
is eligible; ignored classes are always excluded, and disabled heads remain excluded. Select **Apply
and save for this model** to activate and persist the complete mapping. Each class
has exactly one role, preventing conflicting enemy/friendly or head/body assignments.
If the model's class semantics depend on the player's team, update roles manually
when the team changes; the application does not automatically identify the team.

Selections are stored in that model's `.onnx.ini` as a comma-separated `class_roles`
list, in ascending class-ID order. Example for six classes:

```ini
class_roles=friendly_body,enemy_head,ignore,enemy_body,friendly_head,enemy_body
```

Changing models restores its own roles. Old sidecars without `class_roles` migrate
their explicit `class_player`/`class_head` IDs in memory; omitted IDs remain ignored
and never inherit the preceding model's global settings. Existing saved choices are
preserved. New sidecars made by the preparation script start with every class ignored,
regardless of names. Legacy IDs written alongside roles retain only the first enemy
body/head for older readers; the full roles list is authoritative in this build.

A write failure leaves the active selection unchanged and displays the error. The
writer preserves anchors, decoder settings, provenance and other sidecar content.
Applying roles or loading a model invalidates previous tracking identities. The mouse
loop clears cached targets, predictions and queued moves when it observes the change,
including when no new detection frame arrives. These checks do not establish the
accuracy of the underlying model's friend/enemy detections.

Names are parsed as data from ONNX `names` metadata (Python/JSON list or dictionary),
with the sidecar's `names` as a labeled fallback. Without readable names the UI
shows numbered unnamed classes; it does not guess enemy/team/head semantics.
Chinese filenames and labels use an available Windows CJK font.

The page warns when no effective target class is enabled. A head-only selection
cannot work while head targeting is disabled in the **Target** tab. Controls are
disabled during model switching or if the selected model failed to become active;
an older working session is explicitly identified if retained after load failure.

`tools/tests/model_settings_test.cpp` covers metadata parsing and per-model
persistence, including Unicode, class-ID validation, retained decoder settings,
independent model choices and write failures. Run it from a Visual Studio developer
shell using C++17 and the detector include directory; it does not initialize input
devices, capture the screen, or move the mouse.

Mappings in each sidecar are applied when the model loads. `class_head=-1`
means the model has no selected head class; `class_player=-1` disables that
selection until the user identifies the correct label. Unknown semantics are
not inherited from whichever model ran previously.

Legacy saved IDs in the inspected collection (not automatic team identification):

| Models | Player/body | Head |
|---|---:|---:|
| PUBGV8S_640 | 1 | 0 |
| ow2_v8s_2c_320 | 1 (`body`) | -1 |
| sunxds_0.8.0 | 0 | 1 |
| SJZ-V13-320, V8-pro, V8-std | 0 | -1 |

`ow-enemy` / `守望先锋` (`tbody`, `ctbody`), `pubg_v5s_640_2.5w`
(`st`, `t`), `V5-pro/std` (garbled labels), and `PUBG` (no labels) require
confirmation of class semantics. Edit their sidecars after checking the
training labels or labeled sample images. The two Overwatch filenames contain
identical model bytes.

## Raw YOLOv5 limitation

`PUBG-v5s-2000d-300epoch-640.onnx` exposes three raw logit tensors:

```text
output: [1,3,80,80,7]
395:    [1,3,40,40,7]
407:    [1,3,20,20,7]
```

Its graph does not contain the trained anchors. The raw-head decoder is
implemented, but loading this model intentionally fails until its actual
anchors are supplied. For each output, add `anchors.<output-name>` with six
positive comma-separated pixel sizes, ordered `w0,h0,w1,h1,w2,h2`.
For this model the keys are `anchors.output`, `anchors.395`, `anchors.407`.
Use the original training/export model's anchors, converted to pixel units if
stored normalized by stride. Do not substitute anchors from another model.
An original checkpoint or correctly decoded re-export is another solution.

## Verification

After building the DML application:

```powershell
.\build\dml\Release\ai.exe --check-models
```

This checks every ONNX in the executable's `models` directory with the actual
C++ detector at capture sizes 320 and 640, including preprocessing and output
decoding. It starts no capture or input-device threads. A model that only
initializes on the CPU is reported as a failure. Exit status is nonzero when
any model fails, including the expected missing-anchor failure above.

`tools/tests/model_compatibility_test.cpp` exercises layout ambiguity,
objectness multiplication, four-class output, raw YOLOv5 anchors, YOLOX grid
ordering and Unicode sidecars. Compile it with `detector/postProcess.cpp`,
C++17, the detector include directory and OpenCV headers. Run from the repository
root; the unit tests use isolated sidecar fixtures and do not depend on saved model selections.

Graph execution and synthetic-input checks do not establish detection accuracy.
Use labeled images and a reference implementation to validate class semantics,
bounding boxes, confidence thresholds and preprocessing for each training setup.

### Validation on this machine (2026-09-06)

- Release DML application built successfully with MSVC 19.51, Windows SDK
  10.0.26100.0, OpenCV 4.13.0, ONNX Runtime 1.22.0 and DirectML 1.15.4.
- C++ model compatibility unit executable passed.
- Actual `ai.exe --check-models`: 12 passed, 1 failed. The sole failure is
  `PUBG-v5s-2000d-300epoch-640.onnx`, rejected for missing original anchors.
- Separate ONNX Runtime Python 1.24.4 graph checks executed all 13 graphs with
  a DirectML session and produced finite outputs. These checks do not include
  application decoding and therefore do not resolve the missing anchors.

Local outputs: `build/model-check.log`,
`build/dml/Release/models/model-audit.json`, and `build/dml/Release/ai.exe`.
`build/run_local.ps1` records this machine's compiler/SDK paths and invokes the
DML wrapper; it can be used to rebuild while the Visual Studio installer has
not yet registered a complete installation. These paths are machine-specific.


### Overlay language and local verification (2026-10-16)

The active development checkout is `F:\github\sunone_aimbot_2`, based on
upstream `09edb8c6`. The existing upstream Makcu button-role changes are retained.
The local DirectML executable is `build/dml/Release/ai.exe` in this checkout.

按 **Home** 打开设置界面。每个页面顶部的 **Language / 语言** 可切换
**English / 简体中文**；语言偏好保存到 `config.ini` 的 `ui_language`。
当前本机运行配置默认使用中文，通用程序默认语言仍为英文。

在 **AI 模型** 页面查看已加载模型的输入、输出、类别编号与原始名称。
在 **目标类别** 中逐类选择 **忽略 / 敌方身体 / 敌方头部 / 友方身体 / 友方头部**，
点击 **应用并保存到此模型**。同一角色可对应多个类别；没有头部类别就不分配头部。
类别名称保持原文，不按名称推断敌友。未确认的类别可保留“忽略”。
敌方和友方瞄准快捷键分别选择对应阵营；忽略类别及被“禁用爆头”关闭的头部不会参与瞄准。切换模型会恢复各自的设置。
如果同一类别的敌友身份随玩家阵营改变，需要手动调整，程序不会自动判断阵营。

Localization covers the Overlay navigation, settings, statuses, warnings,
statistics and export progress UI. ONNX metadata, filenames and user data retain
their original values. Widget IDs remain stable across language changes.
`tools/overlay_zh-CN.json` is the translation source; regenerate the C++ catalog
with `python tools/generate_overlay_translations.py`, or add `--check` to verify
the generated header and printf argument compatibility.

Validation in the F: checkout:

- DirectML Release build succeeded.
- `tools/run_tests.ps1`: localization, model settings, headless ImGui rendering,
  and decoder compatibility suites passed. Rendering tests check actual ImGui IDs,
  formatted messages and raw `enemy##red` class previews. They do not control the
  desktop or move the mouse.
- `ai.exe --check-models`: all **12 currently present ONNX files passed** inference
  and metadata-snapshot checks (`build/model-check.log`). The previously inspected
  missing-anchor raw YOLOv5 model is absent from this runtime folder and was not
  included in this run. Its compatibility limitation above still applies.
- No in-game detection accuracy or mouse-response acceptance test was performed.

Use a Visual Studio x64 developer shell for `tools/run_tests.ps1` and the normal
build wrappers. The ignored local helper `build/run_local.ps1` contains this
machine's compiler/SDK paths; `-TestsOnly` runs the six C++ test suites.
Models, runtime dependencies, local configuration and build outputs are ignored
by Git.


### Per-class role verification (2026-10-16)

- Settings tests cover all five roles, multiple IDs per role, arbitrary names,
  independent model files, duplicate legacy keys, invalid role lists, and write errors.
- `target_roles_test` links the actual selector and tracker. It covers multiple enemy
  body/head IDs, closer friendlies, ignored/invalid IDs, disabled heads, head/body fusion,
  and invalidation of locked or predicted targets after reclassification/model changes.
- `python tools/tests/test_prepare_roles.py` (requires `onnx` and `numpy`) verifies that
  names such as `enemy`, `head`, `tbody`, `ctbody`, or arbitrary text do not enable targets.
- The local Chinese-only MSVC installation produced a misdecoded include prefix in
  CMake. `build/run_local.ps1` now loads the ignored local `msvc_deps_prefix.cmake`
  through CMake's project include hook, using the actual compiler prefix. A fresh,
  clean rebuild recorded real header dependencies (including `class_roles.h`).
  This workaround is machine-specific; portable source build scripts are unchanged.

The new role editor is available in the DirectML build described above. CUDA/TensorRT
uses the legacy target configuration; its model-inspection editor has not been ported.
No actual game input or native window interaction is exercised by these tests.


### Enemy and friendly hold hotkeys

Open **Home > Buttons / 快捷键**. **Enemy targeting / 敌方瞄准** retains the old
`button_targeting` binding. **Friendly targeting / 友方瞄准** is saved separately as
`button_targeting_friendly` and defaults to `None` for existing and new configurations.
Each binding can contain multiple alternative keys, using the existing + / - controls.

- Hold one team's key to target its assigned body/head classes. With Auto Aim off,
  releasing all aiming controls stops aiming. Ignored classes never become targets.
- Holding both configured teams' keys pauses aiming, including when Auto Aim is on.
  Identical bindings are flagged in the UI and produce the same conflict behavior.
- Auto Aim retains enemy targeting by default. A friendly hold overrides it; releasing
  that key returns to enemy targeting. Legacy device aim acts as an enemy fallback;
  a configured friendly hold overrides that fallback too.
- Team changes clear cached targets, predictions and queued moves. A separate transient
  generation catches rapid A-to-B-to-A transitions without resetting unsaved model roles.
- The existing disable-headshot option applies to both enemy and friendly heads.

`aim_hotkeys_config_test` exercises actual configuration load/save and legacy migration.
The target-role tests exercise both teams, holds/releases/conflicts, auto/device fallback,
multiple body/head IDs, fusion, and stale-lock invalidation including rapid round trips.
Physical keyboard, hardware input adapters, and live game behavior still need user validation.
