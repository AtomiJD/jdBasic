# jdbasic_llm_course.md

## A Course: Neural Networks from Scratch in jdBasic

Welcome! This course will guide you through the core concepts of neural networks by building them from the ground up, using only the powerful array and matrix functions of the `jdBasic` language. By the end of this course, you will understand the fundamental mechanics of how a neural network learns, and you will have a working, trainable Transformer model written entirely in `jdBasic`.

---

## Part I: The Fundamentals

### Chapter 1: The Neuron - The Core Building Block

The most fundamental component of a neural network is the **neuron**. It takes several inputs, multiplies each by a "weight," sums them up, adds a bias, and produces a single output.

* **Inputs**: The data we feed into the neuron.
* **Weights**: Numbers representing the "strength" of each input. These are the values the network will learn.
* **Bias**: A value that allows the neuron to shift its output, helping the network learn patterns.

```basic
' Our neuron has 3 inputs.
INPUTS = [0.5, -1.2, 0.8]
' Every input has a corresponding weight.
WEIGHTS = [0.8, 0.1, -0.4]
' The neuron also has a single bias value.
BIAS = 0.5
' The core calculation: (input * weight) summed up, plus the bias.
OUTPUT = SUM(INPUTS * WEIGHTS) + BIAS
PRINT "Final Neuron Output: "; OUTPUT
````

**What's happening?** We used `jdBasic`'s element-wise array multiplication (`INPUTS * WEIGHTS`) and then `SUM()` to perform the neuron's calculation in two clean steps.

-----

### Chapter 2: A Layer of Neurons & The Forward Pass

A **layer** is a group of neurons that receive the same inputs, each with its own unique weights and bias. We can represent the weights of an entire layer as a matrix and the inputs as a vector. The process of calculating the output of a full layer—the **forward pass**—becomes a single matrix multiplication (`MATMUL`).

```basic
' The same 3 inputs as before, but as a 1x3 matrix.
INPUTS = [[0.5, -1.2, 0.8]]
' A 3x4 weight matrix for 4 neurons.
WEIGHTS = [[0.8, -0.2,  0.3, 1.0], [0.1,  0.4, -0.5, 0.2], [-0.4, 0.9,  0.1, 0.8]]
' A 1x4 bias vector for the 4 neurons.
BIASES = [[0.5, -0.1, 0.2, -0.3]]
' The entire forward pass calculation for all 4 neurons at once!
LAYER_OUTPUTS = MATMUL(INPUTS, WEIGHTS) + BIASES
PRINT "Layer Outputs (1x4):"; LAYER_OUTPUTS
```

**What's happening?** With `MATMUL`, we performed all calculations for the entire layer in a single, readable line. The result is a 1x4 matrix containing the output of each of the four neurons.

-----

### Chapter 3: Activation Functions - Adding Non-Linearity

To learn complex patterns, we need to introduce **non-linearity** using **activation functions**. An activation function is applied to the output of each neuron. The most popular for hidden layers is **ReLU** (Rectified Linear Unit), which is simply `MAX(0, x)`.

```basic
FUNC RELU(X)
  ' (X > 0) creates a boolean array that acts like [0, 1, 1, 1] in math.
  RETURN X * (X > 0)
ENDFUNC

LAYER_OUTPUTS = [[0.04, -0.3, 0.77, -0.1]]
ACTIVATED_RELU = RELU(LAYER_OUTPUTS)
PRINT "Outputs after ReLU Activation:"; ACTIVATED_RELU
```

**What's happening?** Because `jdBasic` supports element-wise operations, we can pass the entire `LAYER_OUTPUTS` array to the `RELU` function to apply the activation to every neuron's output simultaneously.

-----

## Part II: Building Language Models with Tensors

### Chapter 4: The TENSOR Object - Your Autodiff Engine

In Part I, we had to manually calculate the gradients for backpropagation. This gets incredibly complex for larger networks. To build powerful Language Models, we need a better tool.

Enter the `TENSOR` object. A **Tensor** is a multi-dimensional array that automatically keeps track of every operation performed on it. This allows `jdBasic` to perform **Automatic Differentiation** (or "autodiff").

  * `TENSOR.CREATE_LAYER`: A factory that builds model layers (like "DENSE" or "ATTENTION") for us, initializing the weights and biases correctly.
  * `TENSOR.BACKWARD`: The magic function. When you call this on a loss tensor, it automatically calculates the gradients for every single parameter that contributed to that loss, all the way back through your network.
  * `TENSOR.UPDATE`: After `TENSOR.BACKWARD` has calculated the gradients, this function updates your model's weights and biases using the optimizer's rules (e.g., SGD).

With these tools, we can stop worrying about the calculus and start building much more complex architectures.

-----

### Chapter 5: A Recurrent Neural Network (RNN) for Text Generation

Our first advanced model is a **Recurrent Neural Network (RNN)**. Unlike the feed-forward network in Part I, an RNN has a form of **memory**. It processes data sequentially (e.g., one character at a time) and passes a **hidden state** from one step to the next. This hidden state acts as a memory of what it has seen before, allowing it to understand context.

**Goal**: Build a character-level language model that can learn from text and then generate new text in a similar style.

#### Code Sample: A Character-Level RNN

```basic
' ==========================================================
' == A Simple Character-Level Language Model in jdBasic
' ==========================================================

CLS
' --- 1. Vocabulary and Data Setup ---
TEXT_DATA$ = "hello world "

' Create a vocabulary from the unique characters.
VOCAB = [] : FOR I=0 TO LEN(TEXT_DATA$)-1 : C$=MID$(TEXT_DATA$,I+1,1) : IS_IN=0 : if LEN(VOCAB)>0 then : FOR J=0 TO LEN(VOCAB)-1 : IF VOCAB[J]=C$ THEN IS_IN=1 : EXITFOR : ENDIF : NEXT J : Endif : IF IS_IN=0 THEN VOCAB=APPEND(VOCAB,C$) : ENDIF : NEXT I
VOCAB_SIZE = LEN(VOCAB)
VOCAB_MAP = {} : FOR I=0 TO VOCAB_SIZE-1 : VOCAB_MAP{VOCAB[I]}=I : NEXT I

' --- 2. Prepare Training Data ---
INPUT_TOKENS_ARRAY = TENSOR.TOKENIZE(LEFT$(TEXT_DATA$,LEN(TEXT_DATA$)-1), VOCAB_MAP)
TARGET_TOKENS_ARRAY = TENSOR.TOKENIZE(RIGHT$(TEXT_DATA$,LEN(TEXT_DATA$)-1), VOCAB_MAP)

' --- 3. Model Definition ---
HIDDEN_DIM = 64
MODEL = []
MODEL = APPEND(MODEL, TENSOR.CREATE_LAYER("EMBEDDING", {"vocab_size": VOCAB_SIZE, "embedding_dim": HIDDEN_DIM}))
MODEL = APPEND(MODEL, TENSOR.CREATE_LAYER("DENSE", {"input_size": HIDDEN_DIM, "units": HIDDEN_DIM}))
MODEL = APPEND(MODEL, TENSOR.CREATE_LAYER("DENSE", {"input_size": HIDDEN_DIM, "units": HIDDEN_DIM}))
MODEL = APPEND(MODEL, TENSOR.CREATE_LAYER("DENSE", {"input_size": HIDDEN_DIM, "units": VOCAB_SIZE}))
OPTIMIZER = TENSOR.CREATE_OPTIMIZER("SGD", {"learning_rate": 0.05})
EPOCHS = 2500

' --- 4. Helper & Forward Pass Functions ---
FUNC ONE_HOT_ENCODE(token_id, size)
    vec=[] : DIM vec[size] : vec[token_id]=1 : RETURN RESHAPE(vec,[1,size])
ENDFUNC

FUNC FORWARD_PASS(input_token_tensor, prev_h1, prev_h2)
    one_hot = TENSOR.FROM(ONE_HOT_ENCODE(TENSOR.TOARRAY(input_token_tensor)[0], VOCAB_SIZE))
    embedded = TENSOR.MATMUL(one_hot, MODEL[0]{"weights"})
    new_h1 = TENSOR.RELU(TENSOR.MATMUL(embedded+prev_h1, MODEL[1]{"weights"})+MODEL[1]{"bias"})
    new_h2 = TENSOR.RELU(TENSOR.MATMUL(new_h1+prev_h2, MODEL[2]{"weights"})+MODEL[2]{"bias"})
    logits = TENSOR.MATMUL(new_h2, MODEL[3]{"weights"})+MODEL[3]{"bias"}
    RETURN [logits, new_h1, new_h2]
ENDFUNC

FUNC SAMPLE(probs_array)
    r = RND(1) : cdf = 0 : num_probs = LEN(probs_array)[1]
    FOR i = 0 TO num_probs - 1 : cdf=cdf+probs_array[0,i] : IF r<cdf THEN RETURN i
    NEXT i
    RETURN num_probs-1
ENDFUNC

' --- 5. Training Loop ---
FOR EPI = 1 TO EPOCHS
    TOTAL_LOSS = 0
    DIM h1_arr[HIDDEN_DIM]:h1=TENSOR.FROM(RESHAPE(h1_arr,[1,HIDDEN_DIM]))
    DIM h2_arr[HIDDEN_DIM]:h2=TENSOR.FROM(RESHAPE(h2_arr,[1,HIDDEN_DIM]))
    FOR I=0 TO LEN(INPUT_TOKENS_ARRAY)-1
        input = TENSOR.FROM([INPUT_TOKENS_ARRAY[I]])
        target = TENSOR.FROM(ONE_HOT_ENCODE(TARGET_TOKENS_ARRAY[I],VOCAB_SIZE))
        res = FORWARD_PASS(input, h1, h2)
        logits=res[0] : h1=res[1] : h2=res[2]
        loss = TENSOR.CROSS_ENTROPY_LOSS(logits, target)
        TOTAL_LOSS = TOTAL_LOSS + TENSOR.TOARRAY(loss)[0]
        TENSOR.BACKWARD loss
        MODEL = TENSOR.UPDATE(MODEL, OPTIMIZER)
    NEXT I
    IF EPI MOD 100=0 THEN PRINT "Epoch:";EPI;", Loss:";TOTAL_LOSS/LEN(INPUT_TOKENS_ARRAY)
NEXT EPI
```

#### Deconstructing the RNN 🧐

  * **Stateful Forward Pass**: The `FORWARD_PASS` function is the heart of the RNN. Notice how it takes `prev_h1` and `prev_h2` (the hidden states from the *previous* character) as input and returns the new hidden states as output. This state is then fed into the next iteration of the loop, creating the network's memory.
  * **Automatic Training**: The training loop is now much cleaner. We calculate the loss with `TENSOR.CROSS_ENTROPY_LOSS`, then simply call `TENSOR.BACKWARD loss`. This automatically computes the gradients, and `TENSOR.UPDATE` adjusts the model's parameters.
  * **Sampling for Generation**: The `SAMPLE` function takes the model's output probabilities and, instead of just picking the most likely character, it samples from the distribution. This allows for more creative and varied text generation.

-----

### Chapter 6: The Transformer - The Engine of Modern LLMs

RNNs are powerful but have a weakness: they process information one step at a time. This makes it difficult for them to handle very long-term dependencies. The **Transformer** architecture solves this by processing the entire input sequence at once using its magic ingredient: the **Self-Attention** mechanism.

**Goal**: Build a multi-layer Transformer that can learn patterns in text more effectively, incorporating advanced training techniques and a practical save/load workflow.

#### Code Sample: A Multi-Layer Transformer

```basic
' ==========================================================
' == An LLM with a Multi-Layer Transformer in jdBasic
' == Goal: Use stacked, normalized self-attention blocks for stable training.
' ==========================================================

CLS
PRINT "--- Building a Multi-Layer Transformer LLM ---"
PRINT

' --- Helper Functions (declared before use) ---
' NOTE: These functions are already efficient for this BASIC dialect.
' Using more complex vector functions would not improve their performance.
FUNC ONE_HOT_ENCODE_MATRIX(token_array, size)
    rows = LEN(token_array)
    matrix = []
    DIM matrix[rows, size]
    FOR r = 0 TO rows - 1
        FOR c = 0 to size - 1
            matrix[r, c] = 0
        NEXT c
        matrix[r, token_array[r]] = 1
    NEXT r
    RETURN matrix
ENDFUNC

FUNC SAMPLE(probs_array)
    r = RND(1)
    cdf = 0
    num_probs = LEN(probs_array)
    FOR i = 0 TO num_probs - 1
        cdf = cdf + probs_array[i]
        IF r < cdf THEN
            RETURN i
        ENDIF
    NEXT i
    RETURN num_probs - 1
ENDFUNC

' --- 1. Vocabulary and Data Setup ---
' *** MODIFIED: Using more training data is the best way to improve loss ***
'TEXT_DATA$ = "der schnelle braune fuchs springt über den faulen hund. the quick brown fox jumps over the lazy dog. zwei flinke boxer jagen die quirlige eva."
TEXT_DATA$ = TXTREADER$("trainingtext.txt")
PRINT "Vocabulary Data: "; TEXT_DATA$

' NOTE: This method of finding unique characters is clear and effective for a one-time setup.
VOCAB = []
FOR I = 0 TO LEN(TEXT_DATA$) - 1
    CHAR$ = MID$(TEXT_DATA$, I + 1, 1)
    IS_IN_VOCAB = 0
    if LEN(VOCAB) > 0 then
        FOR J = 0 TO LEN(VOCAB) - 1
            IF VOCAB[J] = CHAR$ THEN
                IS_IN_VOCAB = 1
                EXITFOR
            ENDIF
        NEXT J
    Endif
    IF IS_IN_VOCAB = 0 THEN
        VOCAB = APPEND(VOCAB, CHAR$)
    ENDIF
NEXT I

VOCAB_SIZE = LEN(VOCAB)
VOCAB_MAP = {}
FOR I = 0 TO VOCAB_SIZE - 1
    VOCAB_MAP{VOCAB[I]} = I
NEXT I

PRINT "Vocabulary Size:"; VOCAB_SIZE
PRINT


' --- 2. Prepare Training Data ---
INPUT_TOKENS_ARRAY = TENSOR.TOKENIZE(TEXT_DATA$, VOCAB_MAP)
TARGET_TOKENS_ARRAY = APPEND(TAKE(LEN(INPUT_TOKENS_ARRAY) - 1, DROP(1, INPUT_TOKENS_ARRAY)), 0)


' --- 3. Model Definition ---
' *** MODIFIED: Increased model depth for more learning capacity ***
HIDDEN_DIM = 128
EMBEDDING_DIM = HIDDEN_DIM
NUM_LAYERS = 4 ' --- Stacking four Transformer blocks ---

MODEL = {}
MODEL{"embedding"} = TENSOR.CREATE_LAYER("EMBEDDING", {"vocab_size": VOCAB_SIZE, "embedding_dim": EMBEDDING_DIM})
MODEL{"output_norm"} = TENSOR.CREATE_LAYER("LAYER_NORM", {"dim": HIDDEN_DIM})
MODEL{"output"} = TENSOR.CREATE_LAYER("DENSE", {"input_size": HIDDEN_DIM, "units": VOCAB_SIZE})

' Create the stack of Transformer layers
MODEL{"layers"} = []
FOR i = 0 TO NUM_LAYERS - 1
    layer = {}
    layer{"attention"} = TENSOR.CREATE_LAYER("ATTENTION", {"embedding_dim": EMBEDDING_DIM})
    layer{"norm1"} = TENSOR.CREATE_LAYER("LAYER_NORM", {"dim": HIDDEN_DIM})
    layer{"ffn1"} = TENSOR.CREATE_LAYER("DENSE", {"input_size": HIDDEN_DIM, "units": HIDDEN_DIM * 2})
    layer{"ffn2"} = TENSOR.CREATE_LAYER("DENSE", {"input_size": HIDDEN_DIM * 2, "units": HIDDEN_DIM})
    layer{"norm2"} = TENSOR.CREATE_LAYER("LAYER_NORM", {"dim": HIDDEN_DIM})
    MODEL{"layers"} = APPEND(MODEL{"layers"}, layer)
NEXT i


' *** MODIFIED: Tuned hyperparameters for better convergence ***
LEARNING_RATE = 0.1
OPTIMIZER = TENSOR.CREATE_OPTIMIZER("SGD", {"learning_rate": LEARNING_RATE})
EPOCHS = 20000 ' Increased epochs for deeper model

' The core Transformer Block forward pass
' *** MODIFIED: Function now accepts the model to use as a parameter ***
FUNC FORWARD_PASS(current_model, input_tokens)
    SEQ_LEN = LEN(input_tokens)

    ' --- 1. Embedding + Positional Encoding ---
    one_hot_matrix = ONE_HOT_ENCODE_MATRIX(input_tokens, VOCAB_SIZE)
    one_hot_tensor = TENSOR.FROM(one_hot_matrix)
    embedding_weights = current_model{"embedding"}{"weights"}
    x = TENSOR.MATMUL(one_hot_tensor, embedding_weights)
    x = x + TENSOR.POSITIONAL_ENCODING(SEQ_LEN, HIDDEN_DIM)

    ' --- 2. Pass through the stack of Transformer Layers ---
    FOR i = 0 TO NUM_LAYERS - 1
        layer = current_model{"layers"}[i]
        
        ' --- Pre-LN Self-Attention ---
        norm1_out = TENSOR.LAYERNORM(x, layer{"norm1"}{"gain"}, layer{"norm1"}{"bias"})
        attn_layer = layer{"attention"}
        Wq = attn_layer{"Wq"}
        Wk = attn_layer{"Wk"}
        Wv = attn_layer{"Wv"}
        Q = TENSOR.MATMUL(norm1_out, Wq)
        K = TENSOR.MATMUL(norm1_out, Wk)
        V = TENSOR.MATMUL(norm1_out, Wv)
        attn_scores = TENSOR.MATMUL(Q, TENSOR.FROM(TRANSPOSE(TENSOR.TOARRAY(K)))) / SQR(HIDDEN_DIM)
        
        attn_probs = TENSOR.SOFTMAX(attn_scores, TRUE) 

        attention_output = TENSOR.MATMUL(attn_probs, V)
        x = x + attention_output ' Residual connection

        ' --- Pre-LN Feed-Forward ---
        norm2_out = TENSOR.LAYERNORM(x, layer{"norm2"}{"gain"}, layer{"norm2"}{"bias"})
        ffn1_out = TENSOR.RELU(TENSOR.MATMUL(norm2_out, layer{"ffn1"}{"weights"}) + layer{"ffn1"}{"bias"})
        ffn2_out = TENSOR.MATMUL(ffn1_out, layer{"ffn2"}{"weights"}) + layer{"ffn2"}{"bias"}
        x = x + ffn2_out ' Residual connection
    NEXT i
    
    ' --- 3. Final Output ---
    final_norm = TENSOR.LAYERNORM(x, current_model{"output_norm"}{"gain"}, current_model{"output_norm"}{"bias"})
    logits = TENSOR.MATMUL(final_norm, current_model{"output"}{"weights"}) + current_model{"output"}{"bias"}
    
    RETURN logits
ENDFUNC

' --- 5. Training Loop ---
PRINT "--- Starting Training... ---"
PRINT

FOR EPI = 1 TO EPOCHS
    logits_tensor = FORWARD_PASS(MODEL, INPUT_TOKENS_ARRAY)
    target_one_hot_matrix = ONE_HOT_ENCODE_MATRIX(TARGET_TOKENS_ARRAY, VOCAB_SIZE)
    target_tensor = TENSOR.FROM(target_one_hot_matrix)
    
    loss_tensor = TENSOR.CROSS_ENTROPY_LOSS(logits_tensor, target_tensor)
    TENSOR.BACKWARD loss_tensor
    MODEL = TENSOR.UPDATE(MODEL, OPTIMIZER)

    IF EPI MOD 100 = 0 THEN
        PRINT "Epoch:"; EPI; ", Loss:"; TENSOR.TOARRAY(loss_tensor)[0]
    ENDIF
    
    ' *** MODIFIED: Implement learning rate decay for better fine-tuning ***
    IF EPI MOD 2000 = 0 AND LEARNING_RATE > 0.005 THEN
        LEARNING_RATE = LEARNING_RATE / 2
        OPTIMIZER = TENSOR.CREATE_OPTIMIZER("SGD", {"learning_rate": LEARNING_RATE})
        PRINT "New learning rate: "; LEARNING_RATE
    ENDIF
NEXT EPI

PRINT
PRINT "--- Training Complete ---"
PRINT

' --- 6. Save and Load the Trained Model ---

PRINT "--- Saving Model ---"
TENSOR.SAVEMODEL MODEL, "llm_model.json"
PRINT "Model saved to llm_model.json"
PRINT

PRINT "--- Loading Model for Inference ---"
LOADED_MODEL = TENSOR.LOADMODEL("llm_model.json")
IF LEN(LOADED_MODEL)=0 THEN
    PRINT "Failed to load model!"
    END
ENDIF
PRINT "Model loaded successfully."
PRINT

' --- 7. Inference / Text Generation using the LOADED model ---
PRINT "--- Generating Text from Loaded Model ---"
generated_text$ = "Computerwelt"
PRINT "Seed text: "; generated_text$

FOR i = 1 TO 200
    inference_input_tokens = TENSOR.TOKENIZE(generated_text$, VOCAB_MAP)
    
    all_logits_tensor = FORWARD_PASS(LOADED_MODEL, inference_input_tokens)
    all_logits_array = TENSOR.TOARRAY(all_logits_tensor)
    
    last_row_index = LEN(inference_input_tokens) - 1
    last_logits_vector = SLICE(all_logits_array, 0, last_row_index)
    
    last_logits_tensor = TENSOR.FROM(last_logits_vector)
    probs_tensor = TENSOR.SOFTMAX(last_logits_tensor)
    
    probs_array = TENSOR.TOARRAY(probs_tensor)
    
    next_token_id = SAMPLE(probs_array)
    
    generated_text$ = generated_text$ + VOCAB[next_token_id]
    
NEXT i

PRINT
PRINT "Generated Output:"
PRINT generated_text$
PRINT
```

#### Deconstructing the Transformer 🚀

This script demonstrates a complete, modern machine learning workflow.

  * **Scaling Up**: To improve performance, the model is made deeper with `NUM_LAYERS = 4` and a wider `HIDDEN_DIM = 128`. More importantly, it loads its training data from an external file (`trainingtext.txt`), showing how to use larger datasets to get better results.
  * **Causal Masking**: When predicting the next token, the model must not see future tokens. The `TENSOR.SOFTMAX(scores, TRUE)` command enables a **causal mask**, which prevents this "cheating" during training and is critical for text generation.
  * **Learning Rate Decay**: The training loop reduces the learning rate over time. It starts larger to make quick progress and then gets smaller to carefully fine-tune the model's parameters.
  * **Practical Workflow**: The script finishes by saving the fully trained model to a file using `TENSOR.SAVEMODEL`. It then immediately loads it back with `TENSOR.LOADMODEL` and performs inference, proving that the model's knowledge can be stored and reused.

Congratulations\! You have now journeyed from the basics of a single neuron to building, training, and deploying a Transformer model—the foundation of modern AI—all within the `jdBasic` environment. You are well-equipped to experiment, build, and continue your learning journey.

```
```