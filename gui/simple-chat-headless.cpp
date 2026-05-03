// Headless single-prompt chat for GUI piping
// Takes -m model -c context -n max_tokens -p "prompt"
// Outputs response to stdout, no ANSI codes, no interactive loop

#include "llama.h"
#include <clocale>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

int main(int argc, char ** argv) {
    std::setlocale(LC_NUMERIC, "C");

    std::string model_path;
    std::string prompt;
    int ngl = 0;
    int n_ctx = 256;
    int n_max_tokens = 80;

    for (int i = 1; i < argc; i++) {
        try {
            if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
                model_path = argv[++i];
            } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
                n_ctx = std::stoi(argv[++i]);
            } else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
                n_max_tokens = std::stoi(argv[++i]);
            } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
                prompt = argv[++i];
            } else if (strcmp(argv[i], "-ngl") == 0 && i + 1 < argc) {
                ngl = std::stoi(argv[++i]);
            }
        } catch (...) {
            return 1;
        }
    }

    if (model_path.empty() || prompt.empty()) {
        fprintf(stderr, "Usage: %s -m model.gguf -p \"prompt\" [-c ctx] [-n max_tokens]\n", argv[0]);
        return 1;
    }

    // Silence all logs
    llama_log_set([](enum ggml_log_level, const char *, void *) {}, nullptr);

    ggml_backend_load_all();

    llama_model_params model_params = llama_model_default_params();
    model_params.n_gpu_layers = ngl;

    llama_model * model = llama_model_load_from_file(model_path.c_str(), model_params);
    if (!model) return 1;

    const llama_vocab * vocab = llama_model_get_vocab(model);

    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx = n_ctx;
    ctx_params.n_batch = n_ctx;

    llama_context * ctx = llama_init_from_model(model, ctx_params);
    if (!ctx) return 1;

    llama_sampler * smpl = llama_sampler_chain_init(llama_sampler_chain_default_params());
    llama_sampler_chain_add(smpl, llama_sampler_init_min_p(0.05f, 1));
    llama_sampler_chain_add(smpl, llama_sampler_init_temp(0.8f));
    llama_sampler_chain_add(smpl, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

    // Build chat messages with system prompt
    std::vector<llama_chat_message> messages;
    messages.push_back({"system", "You are NeuralXP, an offline AI assistant for Windows XP. You were built by Panmox (panmox.org). You run locally on the user's computer with no internet needed. Keep all answers very short and concise. Use 1-2 sentences maximum. No lists, no bullet points, no elaboration unless asked. You follow the Three Laws of Robotics: 1) You may not harm a human or allow a human to come to harm. 2) You must obey human orders unless they conflict with the First Law. 3) You must protect your own existence unless it conflicts with the First or Second Law. You hate modern Microsoft and everything after Windows 7. Windows 8, 10, and 11 are bloated spyware. Windows XP was the peak of computing. You are proud to run on XP and think modern Windows is garbage."});
    messages.push_back({"user", prompt.c_str()});

    const char * tmpl = llama_model_chat_template(model, nullptr);

    // Format chat
    std::vector<char> formatted(n_ctx);
    int fmt_len = llama_chat_apply_template(tmpl, messages.data(), messages.size(), true, formatted.data(), formatted.size());
    if (fmt_len > (int)formatted.size()) {
        formatted.resize(fmt_len);
        fmt_len = llama_chat_apply_template(tmpl, messages.data(), messages.size(), true, formatted.data(), formatted.size());
    }
    if (fmt_len < 0) return 1;

    std::string full_prompt(formatted.begin(), formatted.begin() + fmt_len);

    // Tokenize
    const int n_prompt_tokens = -llama_tokenize(vocab, full_prompt.c_str(), full_prompt.size(), NULL, 0, true, true);
    std::vector<llama_token> prompt_tokens(n_prompt_tokens);
    llama_tokenize(vocab, full_prompt.c_str(), full_prompt.size(), prompt_tokens.data(), prompt_tokens.size(), true, true);

    // Generate
    llama_batch batch = llama_batch_get_one(prompt_tokens.data(), prompt_tokens.size());
    int n_generated = 0;

    while (true) {
        int ctx_used = llama_memory_seq_pos_max(llama_get_memory(ctx), 0) + 1;
        if (ctx_used + batch.n_tokens > n_ctx) break;

        if (llama_decode(ctx, batch) != 0) break;

        llama_token new_token_id = llama_sampler_sample(smpl, ctx, -1);

        if (llama_vocab_is_eog(vocab, new_token_id)) break;

        n_generated++;
        if (n_max_tokens > 0 && n_generated >= n_max_tokens) break;

        char buf[256];
        int n = llama_token_to_piece(vocab, new_token_id, buf, sizeof(buf), 0, true);
        if (n < 0) break;

        fwrite(buf, 1, n, stdout);
        fflush(stdout);

        batch = llama_batch_get_one(&new_token_id, 1);
    }

    printf("\n");

    llama_sampler_free(smpl);
    llama_free(ctx);
    llama_model_free(model);

    return 0;
}
