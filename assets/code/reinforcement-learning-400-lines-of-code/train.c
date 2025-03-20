#include "common.h"

/* Initialize a neural network with random weights, we should
 * use something like He weights since we use RELU, but we don't
 * care as this is a trivial example. */
#define RANDOM_WEIGHT() (((float)rand() / RAND_MAX) - 0.5f)
void init_neural_network(NeuralNetwork *nn) {
    // Initialize weights with random values between -0.5 and 0.5
    for (int i = 0; i < NN_INPUT_SIZE * NN_HIDDEN_SIZE; i++)
        nn->weights_ih[i] = RANDOM_WEIGHT();

    for (int i = 0; i < NN_HIDDEN_SIZE * NN_OUTPUT_SIZE; i++)
        nn->weights_ho[i] = RANDOM_WEIGHT();

    for (int i = 0; i < NN_HIDDEN_SIZE; i++)
        nn->biases_h[i] = RANDOM_WEIGHT();

    for (int i = 0; i < NN_OUTPUT_SIZE; i++)
        nn->biases_o[i] = RANDOM_WEIGHT();
}

/* Derivative of ReLU activation function */
float relu_derivative(float x) {
    return x > 0 ? 1.0f : 0.0f;
}

/* Backpropagation function.
 * The only difference here from vanilla backprop is that we have
 * a 'reward_scaling' argument that makes the output error more/less
 * dramatic, so that we can adjust the weights proportionally to the
 * reward we want to provide. */
void backprop(NeuralNetwork *nn, float *target_probs, float learning_rate, float reward_scaling) {
    float output_deltas[NN_OUTPUT_SIZE];
    float hidden_deltas[NN_HIDDEN_SIZE];

    /* === STEP 1: Compute deltas === */

    /* Calculate output layer deltas:
     * Note what's going on here: we are technically using softmax
     * as output function and cross entropy as loss, but we never use
     * cross entropy in practice since we check the progresses in terms
     * of winning the game.
     *
     * Still calculating the deltas in the output as:
     *
     *      output[i] - target[i]
     *
     * Is exactly what happens if you derivate the deltas with
     * softmax and cross entropy.
     *
     * LEARNING OPPORTUNITY: This is a well established and fundamental
     * result in neural networks, you may want to read more about it. */
    for (int i = 0; i < NN_OUTPUT_SIZE; i++) {
        output_deltas[i] =
            (nn->outputs[i] - target_probs[i]) * fabsf(reward_scaling);
    }

    // Backpropagate error to hidden layer.
    for (int i = 0; i < NN_HIDDEN_SIZE; i++) {
        float error = 0;
        for (int j = 0; j < NN_OUTPUT_SIZE; j++) {
            error += output_deltas[j] * nn->weights_ho[i * NN_OUTPUT_SIZE + j];
        }
        hidden_deltas[i] = error * relu_derivative(nn->hidden[i]);
    }

    /* === STEP 2: Weights updating === */

    // Output layer weights and biases.
    for (int i = 0; i < NN_HIDDEN_SIZE; i++) {
        for (int j = 0; j < NN_OUTPUT_SIZE; j++) {
            nn->weights_ho[i * NN_OUTPUT_SIZE + j] -=
                learning_rate * output_deltas[j] * nn->hidden[i];
        }
    }
    for (int j = 0; j < NN_OUTPUT_SIZE; j++) {
        nn->biases_o[j] -= learning_rate * output_deltas[j];
    }

    // Hidden layer weights and biases.
    for (int i = 0; i < NN_INPUT_SIZE; i++) {
        for (int j = 0; j < NN_HIDDEN_SIZE; j++) {
            nn->weights_ih[i * NN_HIDDEN_SIZE + j] -=
                learning_rate * hidden_deltas[j] * nn->inputs[i];
        }
    }
    for (int j = 0; j < NN_HIDDEN_SIZE; j++) {
        nn->biases_h[j] -= learning_rate * hidden_deltas[j];
    }
}

/* Train the neural network based on game outcome.
 *
 * The move_history is just an integer array with the index of all the
 * moves. This function is designed so that you can specify if the
 * game was started by the move by the NN or human, but actually the
 * code always let the human move first. */
void learn_from_game(NeuralNetwork *nn, int *move_history, int num_moves, int nn_moves_even, char winner) {
    // Determine reward based on game outcome
    float reward;
    char nn_symbol = nn_moves_even ? 'O' : 'X';

    if (winner == 'T') {
        reward = 0.3f;  // Small reward for draw
    } else if (winner == nn_symbol) {
        reward = 1.0f;  // Large reward for win
    } else {
        reward = -2.0f; // Negative reward for loss
    }

    GameState state;
    float target_probs[NN_OUTPUT_SIZE];

    // Process each move the neural network made.
    for (int move_idx = 0; move_idx < num_moves; move_idx++) {
        // Skip if this wasn't a move by the neural network.
        if ((nn_moves_even && move_idx % 2 != 1) ||
            (!nn_moves_even && move_idx % 2 != 0))
        {
            continue;
        }

        // Recreate board state BEFORE this move was made.
        init_game(&state);
        for (int i = 0; i < move_idx; i++) {
            char symbol = (i % 2 == 0) ? 'X' : 'O';
            state.board[move_history[i]] = symbol;
        }

        // Convert board to inputs and do forward pass.
        float inputs[NN_INPUT_SIZE];
        board_to_inputs(&state, inputs);
        forward_pass(nn, inputs);

        /* The move that was actually made by the NN, that is
         * the one we want to reward (positively or negatively). */
        int move = move_history[move_idx];

        /* Here we can't really implement temporal difference in the strict
         * reinforcement learning sense, since we don't have an easy way to
         * evaluate if the current situation is better or worse than the
         * previous state in the game.
         *
         * However "time related" we do something that is very effective in
         * this case: we scale the reward according to the move time, so that
         * later moves are more impacted (the game is less open to different
         * solutions as we go forward).
         *
         * We give a fixed 0.5 importance to all the moves plus
         * a 0.5 that depends on the move position.
         *
         * NOTE: this makes A LOT of difference. Experiment with different
         * values.
         *
         * LEARNING OPPORTUNITY: Temporal Difference in Reinforcement Learning
         * is a very important result, that was worth the Turing Award in
         * 2024 to Sutton and Barto. You may want to read about it. */
        float move_importance = 0.5f + 0.5f * (float)move_idx/(float)num_moves;
        float scaled_reward = reward * move_importance;

        /* Create target probability distribution:
         * let's start with the logits all set to 0. */
        for (int i = 0; i < NN_OUTPUT_SIZE; i++)
            target_probs[i] = 0;

        /* Set the target for the chosen move based on reward: */
        if (scaled_reward >= 0) {
            /* For positive reward, set probability of the chosen move to
             * 1, with all the rest set to 0. */
            target_probs[move] = 1;
        } else {
            /* For negative reward, distribute probability to OTHER
             * valid moves, which is conceptually the same as discouraging
             * the move that we want to discourage. */
            int valid_moves_left = 9-move_idx-1;
            float other_prob = 1.0f / valid_moves_left;
            for (int i = 0; i < 9; i++) {
                if (state.board[i] == '.' && i != move) {
                    target_probs[i] = other_prob;
                }
            }
        }

        /* Call the generic backpropagation function, using
         * our target logits as target. */
        backprop(nn, target_probs, LEARNING_RATE, scaled_reward);
    }
}

/* Get a random valid move, this is used for training
 * against a random opponent. Note: this function will loop forever
 * if the board is full, but here we want simple code. */
int get_random_move(GameState *state) {
    while(1) {
        int move = rand() % 9;
        if (state->board[move] != '.') continue;
        return move;
    }
}

/* Play a game against random moves and learn from it.
 *
 * This is a very simple Montecarlo Method applied to reinforcement learning:
 *
 * 1. We play a complete random game (episode).
 * 2. We determine the reward based on the outcome of the game.
 * 3. We update the neural network in order to maximize future rewards.
 *
 * LEARNING OPPORTUNITY: while the code uses some Montecarlo-alike
 * technique, important results were recently obtained using
 * Montecarlo Tree Search (MCTS), where a tree structure repesents
 * potential future game states that are explored according to
 * some selection: you may want to learn about it. */
char play_random_game(NeuralNetwork *nn, int *move_history) {
    GameState state;
    char winner = 0;
    int num_moves = 0;

    init_game(&state);

    while (!check_game_over(&state, &winner)) {
        int move;

        if (state.current_player == 0) {  // Random player's turn (X)
            move = get_random_move(&state);
        } else {  // Neural network's turn (O)
            move = get_computer_move(&state, nn, 0);
        }

        /* Make the move and store it: we need the moves sequence
         * during the learning stage. */
        char symbol = (state.current_player == 0) ? 'X' : 'O';
        state.board[move] = symbol;
        move_history[num_moves++] = move;

        // Switch player.
        state.current_player = !state.current_player;
    }

    // Learn from this game - neural network is 'O' (even-numbered moves).
    learn_from_game(nn, move_history, num_moves, 1, winner);
    return winner;
}

/* Train the neural network against random moves. */
void train_against_random(NeuralNetwork *nn, int num_games) {
    int move_history[9];
    int wins = 0, losses = 0, ties = 0;

    printf("Training neural network against %d random games...\n", num_games);

    int played_games = 0;
    for (int i = 0; i < num_games; i++) {
        char winner = play_random_game(nn, move_history);
        played_games++;

        // Accumulate statistics that are provided to the user (it's fun).
        if (winner == 'O') {
            wins++; // Neural network won.
        } else if (winner == 'X') {
            losses++; // Random player won.
        } else {
            ties++; // Tie.
        }

        // Show progress every many games to avoid flooding the stdout.
        if ((i + 1) % 10000 == 0) {
            printf("Games: %d, Wins: %d (%.1f%%), "
                   "Losses: %d (%.1f%%), Ties: %d (%.1f%%)\n",
                  i + 1, wins, (float)wins * 100 / played_games,
                  losses, (float)losses * 100 / played_games,
                  ties, (float)ties * 100 / played_games);
            played_games = 0;
            wins = 0;
            losses = 0;
            ties = 0;
        }
    }
    printf("\nTraining complete!\n");
}

/* Save neural network parameters to a file */
void save_neural_network(NeuralNetwork *nn, const char *filename) {
    FILE *file = fopen(filename, "wb");
    if (file == NULL) {
        printf("Error opening file for writing: %s\n", filename);
        return;
    }
    
    // Write weights and biases
    fwrite(nn->weights_ih, sizeof(float), NN_INPUT_SIZE * NN_HIDDEN_SIZE, file);
    fwrite(nn->weights_ho, sizeof(float), NN_HIDDEN_SIZE * NN_OUTPUT_SIZE, file);
    fwrite(nn->biases_h, sizeof(float), NN_HIDDEN_SIZE, file);
    fwrite(nn->biases_o, sizeof(float), NN_OUTPUT_SIZE, file);
    
    fclose(file);
    printf("Neural network saved to %s\n", filename);
}

int main(int argc, char **argv) {
    int random_games = 150000; // Fast and enough to play in a decent way.
    const char *output_file = "ttt_nn.bin";
    
    if (argc > 1) random_games = atoi(argv[1]);
    if (argc > 2) output_file = argv[2];
    
    srand(time(NULL));

    // Initialize neural network.
    NeuralNetwork nn;
    init_neural_network(&nn);

    printf("Training neural network with %d games\n", random_games);
    
    // Train against random moves.
    if (random_games > 0) train_against_random(&nn, random_games);

    // Save the trained neural network
    save_neural_network(&nn, output_file);
    
    return 0;
}
