# Merge a (Do)LoRA adapter into the bf16 base model with PEFT on CPU.
# Unsloth's GGUF exporter cannot merge DoRA magnitude vectors; PEFT can.
# Config via environment:
#   MERGE_BASE     bf16 base model (default Qwen/Qwen2.5-Coder-14B-Instruct)
#   MERGE_ADAPTER  adapter dir (default out/jdb-r16-e2)
#   MERGE_OUT      merged model output dir (default out/merged-bf16)

import os

import torch
from peft import PeftModel
from transformers import AutoModelForCausalLM, AutoTokenizer

BASE = os.environ.get("MERGE_BASE", "Qwen/Qwen2.5-Coder-14B-Instruct")
ADAPTER = os.environ.get("MERGE_ADAPTER", "out/jdb-r16-e2")
OUT = os.environ.get("MERGE_OUT", "out/merged-bf16")

base = AutoModelForCausalLM.from_pretrained(
    BASE, torch_dtype=torch.bfloat16, device_map="cpu", low_cpu_mem_usage=True
)
model = PeftModel.from_pretrained(base, ADAPTER)
print("[merge_dora] adapter loaded, merging")
merged = model.merge_and_unload()
merged.save_pretrained(OUT, safe_serialization=True)

tokenizer = AutoTokenizer.from_pretrained(ADAPTER)
tokenizer.save_pretrained(OUT)
print(f"[merge_dora] merged model saved to {OUT}")
