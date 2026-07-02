# QLoRA fine-tune of a chat model on the jdBasic ChatML dataset (Unsloth).
# One-command run; config via environment:
#   LORA_BASE      base model (default unsloth/Qwen2.5-Coder-14B-Instruct-bnb-4bit)
#   LORA_DATA      training jsonl, ChatML {"messages":[...]} per line
#   LORA_OUT       output dir for adapter checkpoints
#   LORA_RANK      LoRA rank (default 16)
#   LORA_MAX_STEPS smoke-run step cap; 0 = full epochs (default 0)
#   LORA_EPOCHS    epochs when LORA_MAX_STEPS=0 (default 2)
#   LORA_SEQ_LEN   max sequence length (default 2048)
#   LORA_DORA      1 = enable DoRA (default 1; falls back if unsupported)

import json
import os

import torch
from unsloth import FastLanguageModel
from datasets import Dataset
from trl import SFTConfig, SFTTrainer

BASE = os.environ.get("LORA_BASE", "unsloth/Qwen2.5-Coder-14B-Instruct-bnb-4bit")
DATA = os.environ.get("LORA_DATA", "data/combined_train.jsonl")
OUT = os.environ.get("LORA_OUT", "out/adapter")
RANK = int(os.environ.get("LORA_RANK", "16"))
MAX_STEPS = int(os.environ.get("LORA_MAX_STEPS", "0"))
EPOCHS = float(os.environ.get("LORA_EPOCHS", "2"))
SEQ_LEN = int(os.environ.get("LORA_SEQ_LEN", "2048"))
USE_DORA = os.environ.get("LORA_DORA", "1") == "1"

model, tokenizer = FastLanguageModel.from_pretrained(
    model_name=BASE,
    max_seq_length=SEQ_LEN,
    load_in_4bit=True,
)

peft_kwargs = dict(
    r=RANK,
    lora_alpha=RANK * 2,
    lora_dropout=0.0,
    bias="none",
    target_modules=[
        "q_proj", "k_proj", "v_proj", "o_proj",
        "gate_proj", "up_proj", "down_proj",
    ],
    use_gradient_checkpointing="unsloth",
    random_state=42,
)

if USE_DORA:
    try:
        model = FastLanguageModel.get_peft_model(model, use_dora=True, **peft_kwargs)
        print("[train_lora] DoRA enabled")
    except Exception as e:
        print(f"[train_lora] DoRA unavailable ({e}); falling back to plain LoRA")
        model = FastLanguageModel.get_peft_model(model, **peft_kwargs)
else:
    model = FastLanguageModel.get_peft_model(model, **peft_kwargs)

rows = []
with open(DATA, encoding="utf-8") as f:
    for line in f:
        line = line.strip()
        if not line:
            continue
        msgs = json.loads(line)["messages"]
        rows.append({
            "text": tokenizer.apply_chat_template(
                msgs, tokenize=False, add_generation_prompt=False
            )
        })
print(f"[train_lora] {len(rows)} training examples from {DATA}")

dataset = Dataset.from_list(rows)

config = SFTConfig(
    output_dir=OUT,
    per_device_train_batch_size=1,
    gradient_accumulation_steps=8,
    num_train_epochs=EPOCHS,
    max_steps=MAX_STEPS if MAX_STEPS > 0 else -1,
    learning_rate=2e-4,
    lr_scheduler_type="cosine",
    warmup_ratio=0.03,
    logging_steps=1,
    save_strategy="epoch" if MAX_STEPS == 0 else "no",
    optim="adamw_8bit",
    weight_decay=0.01,
    seed=42,
    bf16=True,
    max_length=SEQ_LEN,
    dataset_text_field="text",
    report_to="none",
)

trainer = SFTTrainer(
    model=model,
    processing_class=tokenizer,
    train_dataset=dataset,
    args=config,
)

stats = trainer.train()
print(f"[train_lora] done: {stats.metrics}")
if torch.cuda.is_available():
    peak = torch.cuda.max_memory_reserved() / (1024 ** 3)
    print(f"[train_lora] peak VRAM reserved: {peak:.2f} GiB")

model.save_pretrained(OUT)
tokenizer.save_pretrained(OUT)
print(f"[train_lora] adapter saved to {OUT}")
