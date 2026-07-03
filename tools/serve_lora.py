# Minimal OpenAI-compatible chat endpoint for a local Unsloth model/adapter.
# Serves POST /v1/chat/completions for the eval harness; single request at a time.
# Config via environment:
#   SERVE_MODEL  model or adapter dir (default out/jdb-r16-e2)
#   SERVE_PORT   listen port (default 8000)
#   SERVE_MAXTOK hard cap on generated tokens (default 1024)

import os

from unsloth import FastLanguageModel
import torch
import uvicorn
from fastapi import FastAPI

MODEL_DIR = os.environ.get("SERVE_MODEL", "out/jdb-r16-e2")
PORT = int(os.environ.get("SERVE_PORT", "8000"))
MAXTOK = int(os.environ.get("SERVE_MAXTOK", "1024"))

model, tokenizer = FastLanguageModel.from_pretrained(
    model_name=MODEL_DIR,
    max_seq_length=4096,
    load_in_4bit=True,
)
FastLanguageModel.for_inference(model)

app = FastAPI()


@app.post("/v1/chat/completions")
def chat(req: dict):
    msgs = req["messages"]
    temperature = float(req.get("temperature", 0.2))
    max_tokens = min(int(req.get("max_tokens", MAXTOK)), MAXTOK)
    inputs = tokenizer.apply_chat_template(
        msgs, tokenize=True, add_generation_prompt=True, return_tensors="pt"
    ).to(model.device)
    with torch.no_grad():
        out = model.generate(
            input_ids=inputs,
            max_new_tokens=max_tokens,
            temperature=max(temperature, 0.01),
            do_sample=temperature > 0,
            pad_token_id=tokenizer.eos_token_id,
        )
    text = tokenizer.decode(out[0][inputs.shape[1]:], skip_special_tokens=True)
    return {
        "choices": [
            {"index": 0, "message": {"role": "assistant", "content": text}}
        ]
    }


if __name__ == "__main__":
    print(f"[serve_lora] serving {MODEL_DIR} on 127.0.0.1:{PORT}")
    uvicorn.run(app, host="127.0.0.1", port=PORT)
