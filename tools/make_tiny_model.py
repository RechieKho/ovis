# Builds a tiny randomly initialised qwen2 GGUF that uses the real Qwen2
# tokenizer. It writes nonsense, but it runs through exactly the same code as
# a real model, so it tests Ovis's in-process inference (loading, the grammar,
# determinism, embedding) without downloading gigabytes.
#
#   pip install numpy gguf
#   curl -LO https://raw.githubusercontent.com/ggml-org/llama.cpp/b11200/models/ggml-vocab-qwen2.gguf
#   python tools/make_tiny_model.py ggml-vocab-qwen2.gguf tiny.gguf
#   OVIS_TEST_MODEL=tiny.gguf ./build/test_ovis
import numpy as np, sys
from gguf import GGUFReader, GGUFWriter, GGUFValueType
src = GGUFReader(sys.argv[1])
def field(name):
    f = src.fields[name]
    if f.types[0] == GGUFValueType.ARRAY:
        if f.types[1] == GGUFValueType.STRING:
            return [bytes(f.parts[i]).decode('utf-8', 'surrogateescape') for i in f.data]
        return [int(f.parts[i][0]) for i in f.data]
    v = f.parts[f.data[0]]
    return bytes(v).decode() if f.types[0] == GGUFValueType.STRING else v[0]
out = sys.argv[2]
w = GGUFWriter(out, 'qwen2')
n_vocab, n_embd, n_ff, n_layer, n_head, n_kv = 151936, 64, 128, 2, 4, 2
w.add_name('tiny-random-qwen2')
w.add_context_length(4096); w.add_embedding_length(n_embd); w.add_block_count(n_layer)
w.add_feed_forward_length(n_ff); w.add_head_count(n_head); w.add_head_count_kv(n_kv)
w.add_rope_freq_base(1e6); w.add_layer_norm_rms_eps(1e-6); w.add_file_type(1)
w.add_tokenizer_model(field('tokenizer.ggml.model')); w.add_tokenizer_pre(field('tokenizer.ggml.pre'))
w.add_token_list(field('tokenizer.ggml.tokens')); w.add_token_types(field('tokenizer.ggml.token_type'))
w.add_token_merges(field('tokenizer.ggml.merges'))
w.add_eos_token_id(151645); w.add_bos_token_id(151643); w.add_pad_token_id(151643)
w.add_chat_template(field('tokenizer.chat_template'))
rng = np.random.default_rng(0)
def t(name, shape, scale=0.02):
    w.add_tensor(name, (rng.standard_normal(shape) * scale).astype(np.float16 if len(shape) > 1 else np.float32))
t('token_embd.weight', (n_vocab, n_embd)); t('output_norm.weight', (n_embd,), 1.0)
t('output.weight', (n_vocab, n_embd), 0.5)
hd = n_embd // n_head
for i in range(n_layer):
    p = f'blk.{i}.'
    t(p+'attn_norm.weight', (n_embd,), 1.0)
    t(p+'attn_q.weight', (n_embd, n_embd)); t(p+'attn_q.bias', (n_embd,))
    t(p+'attn_k.weight', (n_kv*hd, n_embd)); t(p+'attn_k.bias', (n_kv*hd,))
    t(p+'attn_v.weight', (n_kv*hd, n_embd)); t(p+'attn_v.bias', (n_kv*hd,))
    t(p+'attn_output.weight', (n_embd, n_embd)); t(p+'ffn_norm.weight', (n_embd,), 1.0)
    t(p+'ffn_gate.weight', (n_ff, n_embd)); t(p+'ffn_up.weight', (n_ff, n_embd)); t(p+'ffn_down.weight', (n_embd, n_ff))
w.write_header_to_file(); w.write_kv_data_to_file(); w.write_tensors_to_file(); w.close()
print('wrote', out)
