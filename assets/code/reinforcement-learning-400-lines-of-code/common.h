#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <float.h>
#include <string.h>
#include <math.h>

// Neural network parameters.
#define NN_INPUT_SIZE 18
#define NN_HIDDEN_SIZE 100
#define NN_OUTPUT_SIZE 9
#define LEARNING_RATE 0.1

// Game board representation.
typedef struct {
    char board[9];          // Can be "." (empty) or "X", "O".
    int current_player;     // 0 for player (X), 1 for computer (O).
} GameState;

/* Neural network structure. For simplicity we have just
 * one hidden layer and fixed sizes (see defines above).
 * However for this problem going deeper than one hidden layer
 * is useless. */
typedef struct {
    // Weights and biases.
    float weights_ih[NN_INPUT_SIZE * NN_HIDDEN_SIZE];
    float weights_ho[NN_HIDDEN_SIZE * NN_OUTPUT_SIZE];
    float biases_h[NN_HIDDEN_SIZE];
    float biases_o[NN_OUTPUT_SIZE];

    // Activations are part of the structure itself for simplicity.
    float inputs[NN_INPUT_SIZE];
    float hidden[NN_HIDDEN_SIZE];
    float raw_logits[NN_OUTPUT_SIZE]; // Outputs before softmax().
    float outputs[NN_OUTPUT_SIZE];    // Outputs after softmax().
} NeuralNetwork;


/* ReLU activation function */
static float relu(float x) {
    return x > 0 ? x : 0;
}

/* Apply softmax activation function to an array input, and
 * set the result into output. */
static void softmax(float *input, float *output, int size) {
    /* Find maximum value then subtact it to avoid
     * numerical stability issues with exp(). */
    float max_val = input[0];
    for (int i = 1; i < size; i++) {
        if (input[i] > max_val) {
            max_val = input[i];
        }
    }

    // Calculate exp(x_i - max) for each element and sum.
    float sum = 0.0f;
    for (int i = 0; i < size; i++) {
        output[i] = expf(input[i] - max_val);
        sum += output[i];
    }

    // Normalize to get probabilities.
    if (sum > 0) {
        for (int i = 0; i < size; i++) {
            output[i] /= sum;
        }
    } else {
        /* Fallback in case of numerical issues, just provide
         * a uniform distribution. */
        for (int i = 0; i < size; i++) {
            output[i] = 1.0f / size;
        }
    }
}

/* Get the best move for the computer using the neural network.
 * Neural network foward pass (inference). We store the activations
 * so we can also do backpropagation later. */
static void forward_pass(NeuralNetwork *nn, float *inputs) {
    // Copy inputs.
    memcpy(nn->inputs, inputs, NN_INPUT_SIZE * sizeof(float));

    // Input to hidden layer.
    for (int i = 0; i < NN_HIDDEN_SIZE; i++) {
        float sum = nn->biases_h[i];
        for (int j = 0; j < NN_INPUT_SIZE; j++) {
            sum += inputs[j] * nn->weights_ih[j * NN_HIDDEN_SIZE + i];
        }
        nn->hidden[i] = relu(sum);
    }

    // Hidden to output (raw logits).
    for (int i = 0; i < NN_OUTPUT_SIZE; i++) {
        nn->raw_logits[i] = nn->biases_o[i];
        for (int j = 0; j < NN_HIDDEN_SIZE; j++) {
            nn->raw_logits[i] += nn->hidden[j] * nn->weights_ho[j * NN_OUTPUT_SIZE + i];
        }
    }

    // Apply softmax to get the final probabilities.
    softmax(nn->raw_logits, nn->outputs, NN_OUTPUT_SIZE);
}

/* Initialize game state with an empty board. */
void init_game(GameState *state) {
    memset(state->board,'.',9);
    state->current_player = 0;  // Player (X) goes first
}

/* Check if the game is over (win or tie).
 * Very brutal but fast enough. */
static int check_game_over(GameState *state, char *winner) {
    // Check rows.
    for (int i = 0; i < 3; i++) {
        if (state->board[i*3] != '.' &&
            state->board[i*3] == state->board[i*3+1] &&
            state->board[i*3+1] == state->board[i*3+2]) {
            *winner = state->board[i*3];
            return 1;
        }
    }

    // Check columns.
    for (int i = 0; i < 3; i++) {
        if (state->board[i] != '.' &&
            state->board[i] == state->board[i+3] &&
            state->board[i+3] == state->board[i+6]) {
            *winner = state->board[i];
            return 1;
        }
    }

    // Check diagonals.
    if (state->board[0] != '.' &&
        state->board[0] == state->board[4] &&
        state->board[4] == state->board[8]) {
        *winner = state->board[0];
        return 1;
    }
    if (state->board[2] != '.' &&
        state->board[2] == state->board[4] &&
        state->board[4] == state->board[6]) {
        *winner = state->board[2];
        return 1;
    }

    // Check for tie (no free tiles left).
    int empty_tiles = 0;
    for (int i = 0; i < 9; i++) {
        if (state->board[i] == '.') empty_tiles++;
    }
    if (empty_tiles == 0) {
        *winner = 'T';  // Tie
        return 1;
    }

    return 0; // Game continues.
}


/* Convert board state to neural network inputs. Note that we use
 * a peculiar encoding I descrived here:
 * https://www.youtube.com/watch?v=EXbgUXt8fFU
 *
 * Instead of one-hot encoding, we can represent N different categories
 * as different bit patterns. In this specific case it's trivial:
 *
 * 00 = empty
 * 10 = X
 * 01 = O
 *
 * Two inputs per symbol instead of 3 in this case, but in the general case
 * this reduces the input dimensionality A LOT.
 *
 * LEARNING OPPORTUNITY: You may want to learn (if not already aware) of
 * different ways to represent non scalar inputs in neural networks:
 * One hot encoding, learned embeddings, and even if it's just my random
 * exeriment this "permutation coding" that I'm using here.
 */
static void board_to_inputs(GameState *state, float *inputs) {
    for (int i = 0; i < 9; i++) {
        if (state->board[i] == '.') {
            inputs[i*2] = 0;
            inputs[i*2+1] = 0;
        } else if (state->board[i] == 'X') {
            inputs[i*2] = 1;
            inputs[i*2+1] = 0;
        } else {  // 'O'
            inputs[i*2] = 0;
            inputs[i*2+1] = 1;
        }
    }
}

/* Get the best move for the computer using the neural network.
 * Note that there is no complex sampling at all, we just get
 * the output with the highest value THAT has an empty tile. */
static int get_computer_move(GameState *state, NeuralNetwork *nn, int display_probs) {
    float inputs[NN_INPUT_SIZE];

    board_to_inputs(state, inputs);
    forward_pass(nn, inputs);

    // Find the highest probability value and best legal move.
    float highest_prob = -1.0f;
    int highest_prob_idx = -1;
    int best_move = -1;
    float best_legal_prob = -1.0f;

    for (int i = 0; i < 9; i++) {
        // Track highest probability overall.
        if (nn->outputs[i] > highest_prob) {
            highest_prob = nn->outputs[i];
            highest_prob_idx = i;
        }

        // Track best legal move.
        if (state->board[i] == '.' && (best_move == -1 || nn->outputs[i] > best_legal_prob)) {
            best_move = i;
            best_legal_prob = nn->outputs[i];
        }
    }

    // That's just for debugging. It's interesting to show to user
    // in the first iterations of the game, since you can see how initially
    // the net picks illegal moves as best, and so forth.
    if (display_probs) {
        printf("Neural network move probabilities:\n");
        for (int row = 0; row < 3; row++) {
            for (int col = 0; col < 3; col++) {
                int pos = row * 3 + col;

                // Print probability as percentage.
                printf("%5.1f%%", nn->outputs[pos] * 100.0f);

                // Add markers.
                if (pos == highest_prob_idx) {
                    printf("*"); // Highest probability overall.
                }
                if (pos == best_move) {
                    printf("#"); // Selected move (highest valid probability).
                }
                printf(" ");
            }
            printf("\n");
        }

        // Sum of probabilities should be 1.0, hopefully.
        // Just debugging.
        float total_prob = 0.0f;
        for (int i = 0; i < 9; i++)
            total_prob += nn->outputs[i];
        printf("Sum of all probabilities: %.2f\n\n", total_prob);
    }
    return best_move;
}
