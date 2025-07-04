# A Course: Neural Networks from Scratch in jdBasic

Welcome! This course will guide you through the core concepts of neural networks by building one from the ground up, using only the powerful array and matrix functions of the `jdBasic` language.

By the end of this course, you will understand the fundamental mechanics of how a neural network learns, and you will have a working, trainable network written entirely in `jdBasic`.

---

## Part 1: The Neuron - The Core Building Block

The most fundamental component of a neural network is the **neuron** (or *node*). At its simplest, a neuron takes several inputs, multiplies each by a specific "weight," sums them all up, adds a bias, and produces a single output.

* **Inputs**: The data we feed into the neuron.
* **Weights**: Numbers that represent the "strength" of each input. These are the values the network will learn.
* **Bias**: A special value that allows the neuron to shift its output, independent of its inputs. It helps the network learn patterns.

Let's model a single neuron with 3 inputs in `jdBasic`.

### Code Sample 1: A Single Neuron

```basic
' --- Part 1: A Single Neuron ---

PRINT "--- Simulating a Single Neuron ---"
PRINT

' Our neuron has 3 inputs. Let's define them.
INPUTS = [0.5, -1.2, 0.8]

' Every input has a corresponding weight.
WEIGHTS = [0.8, 0.1, -0.4]

' The neuron also has a single bias value.
BIAS = 0.5

PRINT "Inputs: "; INPUTS
PRINT "Weights: "; WEIGHTS
PRINT "Bias: "; BIAS
PRINT

' The core calculation: (input * weight) summed up, plus the bias.
' In jdBasic, we can do this elegantly with element-wise multiplication
' followed by a SUM reduction.
WEIGHTED_SUM = SUM(INPUTS * WEIGHTS)
OUTPUT = WEIGHTED_SUM + BIAS

PRINT "Weighted Sum of Inputs: "; WEIGHTED_SUM
PRINT "Final Neuron Output: "; OUTPUT
```

**What's happening?** We used `jdBasic`'s element-wise array multiplication (`INPUTS * WEIGHTS`) to instantly get `[0.4, -0.12, -0.32]`. Then, `SUM()` reduced this to a single number (`-0.04`), and we added the bias to get our final output.

---

## Part 2: A Layer of Neurons & The Forward Pass

A single neuron is limited. The real power comes from arranging them in **layers**. A layer is simply a group of neurons that all receive the *same* inputs, but each has its own unique set of weights and its own bias.

This is where `jdBasic`'s `MATMUL` (Matrix Multiplication) function becomes essential. We can represent the weights of an entire layer as a matrix, and the inputs as a vector. The "forward pass"—the process of calculating the output of a full layer—becomes a single, clean matrix multiplication.

### Code Sample 2: A Layer of Neurons

```basic
' --- Part 2: A Layer of Neurons ---

PRINT "--- Simulating a Layer of 4 Neurons ---"
PRINT

' The same 3 inputs as before. We can think of this as a 1x3 matrix.
INPUTS = [[0.5, -1.2, 0.8]]

' Now, we have 4 neurons, so we need a weight matrix.
' The shape must be (number of inputs) x (number of neurons).
' So, a 3x4 matrix. Each *column* represents the weights for one neuron.
' Row 1 weights for input 1 to each of the 4 neurons
' Row 2 weights for input 2 to each of the 4 neurons
' Row 3 weights for input 3 to each of the 4 neurons
WEIGHTS = [[0.8, -0.2,  0.3, 1.0],  [0.1,  0.4, -0.5, 0.2],  [-0.4, 0.9,  0.1, 0.8]]  

' Each of the 4 neurons has its own bias. This is a 1x4 vector.
BIASES = [[0.5, -0.1, 0.2, -0.3]]

PRINT "Input Vector (1x3):"
PRINT FRMV$(INPUTS)
PRINT
PRINT "Weight Matrix (3x4):"
PRINT FRMV$(WEIGHTS)
PRINT
PRINT "Bias Vector (1x4):"
PRINT FRMV$(BIASES)
PRINT

' The entire forward pass calculation for all 4 neurons at once!
' The result of INPUTS (1x3) @ WEIGHTS (3x4) is a 1x4 matrix.
LAYER_OUTPUTS = MATMUL(INPUTS, WEIGHTS) + BIASES

PRINT "Layer Outputs (1x4):"
PRINT FRMV$(LAYER_OUTPUTS)
```

**What's happening?** With `MATMUL`, we performed all 12 multiplications and 8 additions required for the entire layer in a single, readable line. The resulting `LAYER_OUTPUTS` matrix contains the output of each of the four neurons. This is the core of a "dense" or "fully-connected" layer.

---

## Part 3: Activation Functions - Adding Non-Linearity

If we only ever do matrix multiplication, our network is just a "linear" model. It can only learn very simple, straight-line relationships. To learn complex patterns (like identifying images or understanding language), we need to introduce **non-linearity**.

We do this with **activation functions**. An activation function is a simple function that we apply to the output of each neuron. Its purpose is to warp the output in a non-linear way.

We'll implement two of the most famous activation functions in `jdBasic`.

### Code Sample 3: Implementing Activation Functions

```basic
' --- Part 3: Activation Functions ---
PRINT "--- Applying Activation Functions ---"
PRINT

' Our raw layer outputs from the previous step.
LAYER_OUTPUTS = [[0.04, 0.1, 0.77, 0.6]]

PRINT "Raw Layer Outputs:"
PRINT FRMV$(LAYER_OUTPUTS)
PRINT

' --- 1. The Sigmoid Function ---
' Sigmoid squashes any number into a range between 0 and 1.
' It's often used for binary classification output layers.
' The formula is: 1 / (1 + e^-x)
FUNC SIGMOID(X)
  ' We multiply by -1 for element-wise negation of the array
  RETURN 1 / (1 + 2.71828 ^ (X * -1))
ENDFUNC

' Apply Sigmoid to the entire output array element-wise.
ACTIVATED_SIGMOID = SIGMOID(LAYER_OUTPUTS)

PRINT "Outputs after Sigmoid Activation:"
PRINT FRMV$(ACTIVATED_SIGMOID)
PRINT


' --- 2. The ReLU (Rectified Linear Unit) Function ---
' ReLU is the most popular activation function for hidden layers.
' It's very simple: if x is negative, it becomes 0. Otherwise, it stays x.
' Formula: MAX(0, x)
FUNC RELU(X)
  ' We can implement this cleverly with array comparisons.
  ' (X > 0) creates a boolean array [FALSE, TRUE, TRUE, TRUE]
  ' which acts like [0, 1, 1, 1] in math.
  RETURN X * (X > 0)
ENDFUNC

' Apply ReLU to the entire output array element-wise.
ACTIVATED_RELU = RELU(LAYER_OUTPUTS)

PRINT "Outputs after ReLU Activation:"
PRINT FRMV$(ACTIVATED_RELU)
```

**What's happening?** We defined `SIGMOID` and `RELU` as standard `jdBasic` functions. Because `jdBasic` supports element-wise operations, we could pass the entire `LAYER_OUTPUTS` array to these functions and have them apply the activation to every neuron's output simultaneously.

---

## Part 4: Training - How a Network Learns

So how do we find the right **weights** and **biases**? We train the network.

The process is:
1.  **Forward Pass**: Feed the network some training data and get its prediction (as we've done above).
2.  **Calculate Loss**: Compare the network's prediction to the *correct* answer. The difference is the "error" or **loss**. A common way to calculate this is *Mean Squared Error*.
3.  **Backpropagation**: This is the magic step. It's a calculus technique that calculates the *gradient* of the loss with respect to every single weight and bias in the network. The gradient tells us "in which direction should we nudge this weight to decrease the error?"
4.  **Update Weights**: We adjust all weights and biases slightly in the direction suggested by the gradients. We use a small **learning rate** to control how big these adjustments are.
5.  **Repeat**: We repeat this process thousands of times with all our training data. Slowly, the weights converge to values that produce good predictions.

While `jdBasic` doesn't have an automatic differentiation engine, we can *manually* implement the gradient equations for a simple network.

---

## Part 5: Putting It All Together - A Trainable Network

Let's create a complete network in `jdBasic` that learns to solve the classic XOR logic problem. This is a problem that cannot be solved by a simple linear model, proving our network can learn complex patterns.

### Code Sample 4: Training a Network for XOR

```basic
' ==========================================================
' == A Complete, Trainable Neural Network in jdBasic
' == Goal: Learn the XOR logic gate
' ==========================================================

' --- 1. Setup and Helper Functions ---

' The Sigmoid activation function
FUNC SIGMOID(X)
  ' We multiply by -1 for element-wise negation of the array
  RETURN 1 / (1 + 2.71828 ^ (X * -1))
ENDFUNC

' The *derivative* of the Sigmoid function.
' This is needed for backpropagation.
FUNC SIGMOID_DERIVATIVE(X)
  RETURN X * (1 - X)
ENDFUNC

' Helper function to create an array of a given SHAPE filled with random numbers.
FUNC RAND_ARRAY(SHAPE)
  TOTAL_ELEMENTS = PRODUCT(SHAPE)
  DIM R[TOTAL_ELEMENTS]
  FOR i = 0 TO TOTAL_ELEMENTS - 1
    R[i] = RND(1)
  NEXT i
  RETURN RESHAPE(R, SHAPE)
ENDFUNC


' --- 2. Training Data and Network Architecture ---

' XOR truth table:
' Input: [0,0], Output: [0]
' Input: [0,1], Output: [1]
' Input: [1,0], Output: [1]
' Input: [1,1], Output: [0]
TRAINING_INPUTS = [[0,0], [0,1], [1,0], [1,1]]
TRAINING_OUTPUTS = [[0], [1], [1], [0]]

' Initialize weights and biases with random numbers shifted to the range [-1, 1].
' Hidden Layer: 2 input neurons, 3 hidden neurons
' Output Layer: 3 hidden neurons, 1 output neuron
WEIGHTS_HIDDEN = (RAND_ARRAY([2, 3]) - 0.5) * 2
WEIGHTS_OUTPUT = (RAND_ARRAY([3, 1]) - 0.5) * 2
BIAS_HIDDEN = (RAND_ARRAY([1, 3]) - 0.5) * 2
BIAS_OUTPUT = (RAND_ARRAY([1, 1]) - 0.5) * 2

LEARNING_RATE = 0.1
EPOCHS = 10000 ' Number of training iterations

PRINT "--- Starting Training ---"

' --- 3. The Training Loop ---

FOR i = 1 TO EPOCHS
  ' --- FORWARD PASS ---
  HIDDEN_LAYER_ACTIVATION = MATMUL(TRAINING_INPUTS, WEIGHTS_HIDDEN) + BIAS_HIDDEN
  ACTIVATED_HIDDEN = SIGMOID(HIDDEN_LAYER_ACTIVATION)
  
  OUTPUT_LAYER_ACTIVATION = MATMUL(ACTIVATED_HIDDEN, WEIGHTS_OUTPUT) + BIAS_OUTPUT
  PREDICTED_OUTPUT = SIGMOID(OUTPUT_LAYER_ACTIVATION)

  ' --- BACKPROPAGATION ---
  ' Calculate the error
  ERROR = TRAINING_OUTPUTS - PREDICTED_OUTPUT

  ' Calculate the gradients (this is the derived calculus)
  D_PREDICTED_OUTPUT = ERROR * SIGMOID_DERIVATIVE(PREDICTED_OUTPUT)
  
  ERROR_HIDDEN_LAYER = MATMUL(D_PREDICTED_OUTPUT, TRANSPOSE(WEIGHTS_OUTPUT))
  D_HIDDEN_LAYER = ERROR_HIDDEN_LAYER * SIGMOID_DERIVATIVE(ACTIVATED_HIDDEN)

  ' --- UPDATE WEIGHTS AND BIASES ---
  ' Update output layer weights
  WEIGHTS_OUTPUT = WEIGHTS_OUTPUT + MATMUL(TRANSPOSE(ACTIVATED_HIDDEN), D_PREDICTED_OUTPUT) * LEARNING_RATE
  
  ' Update hidden layer weights
  WEIGHTS_HIDDEN = WEIGHTS_HIDDEN + MATMUL(TRANSPOSE(TRAINING_INPUTS), D_HIDDEN_LAYER) * LEARNING_RATE
  
  ' Update biases
  BIAS_OUTPUT = BIAS_OUTPUT + SUM(D_PREDICTED_OUTPUT, 0) * LEARNING_RATE
  BIAS_HIDDEN = BIAS_HIDDEN + SUM(D_HIDDEN_LAYER, 0) * LEARNING_RATE

  ' Print progress every 1000 epochs
  IF i MOD 1000 = 0 THEN
    LOSS = SUM(ERROR ^ 2) / LEN(ERROR)[0]
    PRINT "Epoch:"; i; ", Loss:"; LOSS
  ENDIF
NEXT i

PRINT
PRINT "--- Training Complete ---"
PRINT "Final Predictions:"
PRINT FRMV$(PREDICTED_OUTPUT)
```

**What's happening?** This code implements the full training loop. The `FORWARD PASS` section is exactly what we built in Parts 2 and 3. The `BACKPROPAGATION` section implements the derived gradient equations using `jdBasic`'s powerful matrix functions (`MATMUL`, `TRANSPOSE`, and element-wise `*`). Finally, the `UPDATE` section nudges the weights and biases in the correct direction.

Congratulations! You have just walked through the creation and training of a neural network using the unique and powerful capabilities of `jdBasic`.