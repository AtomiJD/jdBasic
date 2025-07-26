# A Course: Neural Networks from Scratch in jdBasic

Welcome\! This course will guide you through the core concepts of neural networks by building them from the ground up, using only the powerful array and matrix functions of the `jdBasic` language. By the end of this course, you will understand the fundamental mechanics of how a neural network learns, and you will have a working, trainable Transformer model written entirely in `jdBasic`.

-----

## Part I: The Fundamentals

### Chapter 1: The Neuron - The Core Building Block

The most fundamental component of a neural network is the **neuron**. It takes several inputs, multiplies each by a "weight," sums them up, adds a bias, and produces a single output.

  * **Inputs**: The data we feed into the neuron.
  * **Weights**: Numbers representing the "strength" of each input. These are the values the network will learn.
  * **Bias**: A value that allows the neuron to shift its output, helping the network learn patterns.

<!-- end list -->

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
```

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

### Chapter 6: The Transformer - The AI Artist

RNNs are powerful, but they process information one step at a time. The **Transformer** architecture solves this by processing the entire input sequence at once using its magic ingredient: the **Self-Attention** mechanism. This allows it to model complex relationships between distant tokens in the sequence.

**Goal**: Build a generative AI that learns a simple "drawing language" and then uses that knowledge to create its own unique, abstract art. This project combines all the concepts we've learned into a practical, creative application.

#### Code Sample: The AI Artist Transformer

```basic
' ===================================================================
' == AI Artist for jdBasic
' ==
' == This program demonstrates the power of jdBasic's built-in
' == Tensor engine by training a Transformer Language Model to
' == generate abstract art.
' ==
' == HOW IT WORKS:
' == 1. A simple "drawing language" is defined (e.g., "C 100 200 50 ...").
' == 2. A Transformer LLM is trained on examples of this language.
' == 3. The trained model is then used to generate new, unique
' ==    sequences of drawing commands, creating generative art.
' ===================================================================

'TRON
TROFF

' ===================================================================
' == SHARED HELPER FUNCTIONS (Used by Training and Inference)
' ===================================================================

FUNC FORWARD_PASS(current_model, input_tokens)
    SEQ_LEN = LEN(input_tokens)[0] ' <-- FIX: Use LEN(...)[0]
    HIDDEN_DIM = LEN(TENSOR.TOARRAY(current_model{"output_norm"}{"bias"}))[1]

    one_hot = ONE_HOT_ENCODE_MATRIX(input_tokens, VOCAB_SIZE)
    x = MATMUL(TENSOR.FROM(one_hot), current_model{"embedding"}{"weights"})

    x = x + TENSOR.POSITIONAL_ENCODING(SEQ_LEN, HIDDEN_DIM)

    FOR i = 0 TO LEN(current_model{"layers"})[0] - 1 ' <-- FIX: Use LEN(...)[0]
        layer = current_model{"layers"}[i]
        norm1_out = TENSOR.LAYERNORM(x, layer{"norm1"}{"gain"}, layer{"norm1"}{"bias"})
        
        Q = MATMUL(norm1_out, layer{"attention"}{"Wq"})
        K = MATMUL(norm1_out, layer{"attention"}{"Wk"})
        V = MATMUL(norm1_out, layer{"attention"}{"Wv"})
        
        attn_scores = MATMUL(Q, TENSOR.FROM(TRANSPOSE(TENSOR.TOARRAY(K)))) / SQR(HIDDEN_DIM)
        attn_probs = TENSOR.SOFTMAX(attn_scores, TRUE)
        attention_output = MATMUL(attn_probs, V)
        x = x + attention_output

        norm2_out = TENSOR.LAYERNORM(x, layer{"norm2"}{"gain"}, layer{"norm2"}{"bias"})
        ffn1_out = TENSOR.RELU(MATMUL(norm2_out, layer{"ffn1"}{"weights"}) + layer{"ffn1"}{"bias"})
        ffn2_out = MATMUL(ffn1_out, layer{"ffn2"}{"weights"}) + layer{"ffn2"}{"bias"}
        x = x + ffn2_out
    NEXT i
    
    final_norm = TENSOR.LAYERNORM(x, current_model{"output_norm"}{"gain"}, current_model{"output_norm"}{"bias"})
    logits = MATMUL(final_norm, current_model{"output"}{"weights"}) + current_model{"output"}{"bias"}
    
    RETURN logits
ENDFUNC

FUNC ONE_HOT_ENCODE_MATRIX(token_array, size)
    rows = LEN(token_array)[0] ' <-- FIX: Extract the scalar size
    matrix = RESHAPE([0], [rows, size])
    FOR r = 0 TO rows - 1
        matrix[r, token_array[r]] = 1
    NEXT r
    RETURN matrix
ENDFUNC

FUNC SAMPLE(probs_array)
    r = RND(1) 
    cdf = 0
    num_probs = LEN(probs_array)[0] ' <-- FIX: Use LEN(...)[0]
    FOR i = 0 TO num_probs - 1
        cdf = cdf + probs_array[i]
        IF r < cdf THEN RETURN i
    NEXT i
    RETURN num_probs - 1
ENDFUNC

' --- A smarter sampling function to improve generation quality ---
FUNC SAMPLE_TOP_K(probs_array, k)
    ' 1. Get the indices that would sort the probabilities in descending order
    sorted_indices = REVERSE(grade(probs_array))
    
    ' 2. Take the top 'k' indices and their corresponding probabilities
    top_indices = TAKE(k, sorted_indices)
    top_probs = []
    FOR i = 0 TO LEN(top_indices)[0] - 1
        top_probs = APPEND(top_probs, probs_array[top_indices[i]])
    NEXT i
    
    ' 3. Re-normalize the top k probabilities so they sum to 1
    total_prob = SUM(top_probs)
    IF total_prob = 0 THEN RETURN top_indices[0] ' Failsafe
    renormalized_probs = top_probs / total_prob
    
    ' 4. Sample from this smaller, cleaner distribution
    r = RND(1)
    cdf = 0
    num_probs = LEN(renormalized_probs)[0]
    FOR i = 0 TO num_probs - 1
        cdf = cdf + renormalized_probs[i]
        IF r < cdf THEN RETURN top_indices[i] ' Return the ORIGINAL index
    NEXT i
    RETURN top_indices[num_probs - 1] ' Failsafe
ENDFUNC


' ===================================================================
' == PART 1: TRAINING THE AI MODEL (The "Brain" of the Artist)
' ===================================================================
'
' NOTE: This section is commented out by default because training
' can take time. A pre-trained 'art_model.json' is expected to
' exist. To train the model yourself, simply remove the initial
' GOTO statement and the REM comments.

'GOTO SKIP_TRAINING
l = len(dir$("_art_model.json"))[0]
if len(dir$("_art_model.json"))[0] > 0 then
    input "Training data found, skip learning?"; a$
    if ucase$(left$(a$,1)) = "Y" then
        GOTO SKIP_TRAINING
    endif
ELSE
    Print "No training data found."
Endif

REM --- Training Data: A simple language for drawing shapes ---
REM Format: "TYPE X Y SIZE R G B;"
REM C=Circle, L=Line, R=Rectangle
TRAINING_DATA$ = "C 400 300 100 255 0 0; L 100 100 700 500 0 255 0; R 200 150 400 300 120 0 255;"
TRAINING_DATA$ = TRAINING_DATA$ + "C 150 450 80 255 255 0; L 50 550 750 50 255 0 255; R 600 100 120 0 255 255;"
TRAINING_DATA$ = TRAINING_DATA$ + "L 200 150 21 55 0 150 0; R 400 500 180 50 75 0 150; L 300 200 420 120 150 255 0;"
PRINT "--- 1. Setting up Vocabulary and Training Data ---"
' Create a vocabulary of all unique characters in our drawing language.

REM --- Procedurally Generate a Large, Varied Training Dataset ---
PRINT "--- 1. Generating Diverse Training Data ---"

REM TXTWRITER "artai_training_data.txt", TRAINING_DATA$

VOCAB = []
FOR I = 0 TO LEN(TRAINING_DATA$) - 1
    CHAR$ = MID$(TRAINING_DATA$, I + 1, 1)
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

VOCAB_SIZE = LEN(VOCAB)[0] ' <-- FIX: Extract the scalar size

' Create a Map for fast character-to-index lookup.
VOCAB_MAP = {}
FOR I = 0 TO VOCAB_SIZE - 1
    VOCAB_MAP{VOCAB[I]} = I
NEXT I

PRINT "Vocabulary Size:"; VOCAB_SIZE

' Prepare the input and target sequences for the LLM.
' The target is the input shifted by one character.
INPUT_TOKENS = TENSOR.TOKENIZE(TRAINING_DATA$, VOCAB_MAP)
TARGET_TOKENS = APPEND(TAKE(LEN(INPUT_TOKENS)[0] - 1, DROP(1, INPUT_TOKENS)), 0) ' <-- FIX: Use LEN(...)[0]

PRINT "--- 2. Building the Transformer Model ---"
HIDDEN_DIM = 64
EMBEDDING_DIM = HIDDEN_DIM
NUM_LAYERS = 2 ' A 2-layer Transformer is sufficient for this task

MODEL = {}
MODEL{"embedding"} = TENSOR.CREATE_LAYER("EMBEDDING", {"vocab_size": VOCAB_SIZE, "embedding_dim": EMBEDDING_DIM})
MODEL{"output_norm"} = TENSOR.CREATE_LAYER("LAYER_NORM", {"dim": HIDDEN_DIM})
MODEL{"output"} = TENSOR.CREATE_LAYER("DENSE", {"input_size": HIDDEN_DIM, "units": VOCAB_SIZE})
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

' --- 3. The Training Loop ---
PRINT "--- 3. Starting Training (this may take a few minutes)... ---"
OPTIMIZER = TENSOR.CREATE_OPTIMIZER("SGD", {"learning_rate": 0.05})
EPOCHS = 5000

FOR EPI = 1 TO EPOCHS
    ' Forward pass through the entire model
    logits_tensor = FORWARD_PASS(MODEL, INPUT_TOKENS)
    
    ' Prepare target tensor
    target_one_hot = ONE_HOT_ENCODE_MATRIX(TARGET_TOKENS, VOCAB_SIZE)
    target_tensor = TENSOR.FROM(target_one_hot)
    
    ' Calculate loss, backpropagate, and update the model
    loss_tensor = TENSOR.CROSS_ENTROPY_LOSS(logits_tensor, target_tensor)
    TENSOR.BACKWARD loss_tensor
    MODEL = TENSOR.UPDATE(MODEL, OPTIMIZER)

    IF EPI MOD 100 = 0 THEN
        PRINT "Epoch:"; EPI; ", Loss: "; TENSOR.TOARRAY(loss_tensor)[0]
    ENDIF
NEXT EPI

PRINT "--- 4. Training Complete. Saving model... ---"
' --- FIX: Save the vocabulary along with the model parameters ---
MODEL{"vocab"} = VOCAB
MODEL{"vocab_map"} = VOCAB_MAP
TENSOR.SAVEMODEL MODEL, "_art_model.json"
PRINT "Model saved to _art_model.json"
PRINT ""


' ===================================================================
' == PART 2: THE GENERATIVE ARTIST
' ===================================================================

SKIP_TRAINING:

' --- 2. Setup Graphics and Generation Loop ---
SCREEN 800, 600, "jdBasic AI Artist"
CLS 10, 15, 20 ' Dark background

PRINT "--- AI Artist Initializing ---"
' --- 1. Load the Pre-Trained Model ---
PRINT "Loading pre-trained AI model 'art_model.json'..."
ARTIST_MODEL = TENSOR.LOADMODEL("_art_model.json")
IF LEN(ARTIST_MODEL) = 0 THEN
    PRINT "ERROR: Could not load 'art_model.json'."
    PRINT "Please run the training section first by removing the GOTO."
    END
ENDIF
PRINT "Model loaded successfully."
' --- FIX: Re-create the vocabulary from the loaded model file ---
VOCAB = ARTIST_MODEL{"vocab"}
VOCAB_MAP = ARTIST_MODEL{"vocab_map"}
VOCAB_SIZE = LEN(VOCAB)[0] ' <-- FIX: Re-initialize global vars from loaded model

generated_command$ = "C" ' Start with a seed to generate a Circle
PRINT "Seed command: '"; generated_command$; "'"
PRINT "Press ESC to quit."

DO
    ' --- a. Generate the next character using the AI ---
    inference_tokens = TENSOR.TOKENIZE(generated_command$, VOCAB_MAP)
    all_logits = FORWARD_PASS(ARTIST_MODEL, inference_tokens)
    
    ' Get the logits for the very last character in the sequence
    last_logits_vec = SLICE(TENSOR.TOARRAY(all_logits), 0, LEN(inference_tokens)[0] - 1) 
    TEMPERATURE = 1.2
    last_logits_vec = last_logits_vec / TEMPERATURE

    ' Convert logits to probabilities and sample the next character
    'probs = TENSOR.SOFTMAX(TENSOR.FROM(last_logits_vec))
    'next_token_id = SAMPLE(TENSOR.TOARRAY(probs))
    probs = TENSOR.TOARRAY(TENSOR.SOFTMAX(TENSOR.FROM(last_logits_vec))) 
    next_token_id = SAMPLE_TOP_K(probs, 5)
    next_char$ = VOCAB[next_token_id]
    
    generated_command$ = generated_command$ + next_char$
   
    ' --- b. Check if a command is complete ---
    IF next_char$ = ";" THEN
        PRINT "AI Generated Command: "; generated_command$
        
        ' --- c. Parse and Execute the Drawing Command ---
        parts = SPLIT(TRIM$(LEFT$(generated_command$, LEN(generated_command$)-1)), " ")
        
        IF LEN(parts)[0] = 7 THEN 
            TYPE$ = UCASE$(parts[0])
            x = VAL(parts[1]) 
            y = VAL(parts[2]) 
            size = VAL(parts[3])
            r = VAL(parts[4]) 
            g = VAL(parts[5]) 
            b = VAL(parts[6])

            IF TYPE$ = "C" THEN
                CIRCLE x, y, size, r, g, b
            ENDIF
            IF TYPE$ = "R" THEN
                RECT x, y, size, size/2, r, g, b, TRUE
            ENDIF
            IF TYPE$ = "L" THEN
                ' For lines, interpret 'size' as a second point offset
                LINE x, y, x+size, y+size, r, g, b
            ENDIF
            
            SCREENFLIP
            generated_command$ = parts[0]
        ELSE 
            PRINT "Invalid command generated. Resetting seed..."
            ' Reset with a random shape to encourage variety
            shape_choice = RND(1)
            IF shape_choice < 0.33 THEN
                generated_command$ = "C"
            ELSEIF shape_choice < 0.66 THEN 
                generated_command$ = "R"
            ELSE
                generated_command$ = "L"
            ENDIF
        ENDIF
    ENDIF

    ' Check for exit key
    IF INKEY$() = CHR$(27) THEN EXITDO
    SLEEP 10 ' Small delay
LOOP
```

#### Deconstructing the AI Artist 🚀

This script demonstrates a complete, modern machine learning workflow.

  * **A Creative Goal**: Instead of just predicting text, this model learns the syntax of a simple graphical language. Its goal is to generate new, valid commands that result in abstract art. 
  * **Self-Attention in Action**: The `FORWARD_PASS` function is a pure Transformer. It uses self-attention (`Q`, `K`, `V` matrices) to weigh the importance of all previous characters when predicting the next one. This is what allows it to learn complex grammatical rules, like how many numbers should follow a "C". 
  * **Robust Training**: The model is trained on a set of example drawing commands. The training loop calculates the `CROSS_ENTROPY_LOSS`, backpropagates the error with `TENSOR.BACKWARD`, and updates the model with `TENSOR.UPDATE`. 
  * **Creative Generation**: During inference, the AI uses a `TEMPERATURE` variable to make its predictions less deterministic. A higher temperature allows for more surprising (and creative) outputs. It also includes error handling: if it generates a syntactically incorrect command, it resets itself with a new random seed, preventing it from getting stuck in a feedback loop. 
  * **Practical Workflow**: The script finishes by saving the fully trained model to `_art_model.json` using `TENSOR.SAVEMODEL`. It then immediately loads it back with `TENSOR.LOADMODEL` and performs inference, proving that the model's knowledge can be stored and reused. 

Congratulations\! You have now journeyed from the basics of a single neuron to building, training, and deploying a creative Transformer model—the foundation of modern AI—all within the `jdBasic` environment. You are well-equipped to experiment, build, and continue your learning journey.