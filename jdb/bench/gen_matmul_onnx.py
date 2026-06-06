"""
Generate a tiny ONNX model with a single MatMul op and dynamic shapes.

Usage:  python3 jdb/bench/gen_matmul_onnx.py
Output: jdb/bench/matmul.onnx

Model:
  Input  A: float32 [M, K]
  Input  B: float32 [K, N]
  Output Y: float32 [M, N]   (Y = MatMul(A, B))

All three dims are symbolic (M, K, N) so the same .onnx can be used
for any matrix size at runtime via ONNX Runtime's dynamic-axis support.
"""
import onnx
from onnx import helper, TensorProto

A = helper.make_tensor_value_info("A", TensorProto.FLOAT, ["M", "K"])
B = helper.make_tensor_value_info("B", TensorProto.FLOAT, ["K", "N"])
Y = helper.make_tensor_value_info("Y", TensorProto.FLOAT, ["M", "N"])

node = helper.make_node("MatMul", inputs=["A", "B"], outputs=["Y"], name="matmul0")

graph = helper.make_graph(
    nodes=[node],
    name="matmul_graph",
    inputs=[A, B],
    outputs=[Y],
)

# opset 13 covers MatMul on float, accepted by all recent onnxruntime versions.
opset = helper.make_opsetid("", 13)
model = helper.make_model(graph, producer_name="jdbasic-bench", opset_imports=[opset])
model.ir_version = 7

onnx.checker.check_model(model)
out_path = "jdb/bench/matmul.onnx"
onnx.save(model, out_path)
print(f"wrote {out_path} ({len(model.SerializeToString())} bytes)")
