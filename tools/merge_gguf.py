# Merge a LoRA adapter into its base model and export a quantized GGUF.
# Config via environment:
#   MERGE_ADAPTER  adapter dir (default out/jdb-r16-e2)
#   MERGE_OUT      output dir (default out/gguf)
#   MERGE_QUANT    llama.cpp quantization method (default q4_k_m)

import os

from unsloth import FastLanguageModel

ADAPTER = os.environ.get("MERGE_ADAPTER", "out/jdb-r16-e2")
OUT = os.environ.get("MERGE_OUT", "out/gguf")
QUANT = os.environ.get("MERGE_QUANT", "q4_k_m")

model, tokenizer = FastLanguageModel.from_pretrained(
    model_name=ADAPTER,
    max_seq_length=4096,
    load_in_4bit=True,
)

model.save_pretrained_gguf(OUT, tokenizer, quantization_method=QUANT)
print(f"[merge_gguf] GGUF ({QUANT}) written to {OUT}")
