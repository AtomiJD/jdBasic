"""
Generate a tiny ONNX model with a single Conv2D op (3x3 kernel).

Usage:  python3 jdb/bench/gen_conv_onnx.py
Output: jdb/bench/conv3x3.onnx

Model:
  Input  X : float32 [1, 1, H, W]    (single grayscale image)
  Input  W : float32 [1, 1, 3, 3]    (kernel)
  Output Y : float32 [1, 1, H, W]    (same-size output via SAME padding)

Kernel is an INPUT (not baked-in), so the same model can apply box-blur,
edge detection, sharpen, etc. by just passing different W tensors.

H, W are dynamic so any image size works.
"""
import onnx
from onnx import helper, TensorProto

X = helper.make_tensor_value_info("X", TensorProto.FLOAT, [1, 1, "H", "W"])
W = helper.make_tensor_value_info("W", TensorProto.FLOAT, [1, 1, 3, 3])
Y = helper.make_tensor_value_info("Y", TensorProto.FLOAT, [1, 1, "H", "W"])

# pads = [top, left, bottom, right] = [1, 1, 1, 1] for 'SAME' on 3x3 stride-1.
node = helper.make_node(
    "Conv",
    inputs=["X", "W"],
    outputs=["Y"],
    kernel_shape=[3, 3],
    pads=[1, 1, 1, 1],
    strides=[1, 1],
    name="conv0",
)

graph = helper.make_graph([node], "conv_graph", inputs=[X, W], outputs=[Y])
opset = helper.make_opsetid("", 13)
model = helper.make_model(graph, producer_name="jdbasic-bench", opset_imports=[opset])
model.ir_version = 7

onnx.checker.check_model(model)
out = "jdb/bench/conv3x3.onnx"
onnx.save(model, out)
print(f"wrote {out} ({len(model.SerializeToString())} bytes)")
