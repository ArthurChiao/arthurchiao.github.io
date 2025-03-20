#include "common.h"

/* Show board on screen in ASCII "art"... */
static void display_board(GameState *state) {
    for (int row = 0; row < 3; row++) {
        // Display the board symbols.
        printf("%c%c%c ", state->board[row*3], state->board[row*3+1],
                          state->board[row*3+2]);

        // Display the position numbers for this row, for the poor human.
        printf("%d%d%d\n", row*3, row*3+1, row*3+2);
    }
    printf("\n");
}


/* Play one game of Tic Tac Toe against the neural network. */
void play_game(NeuralNetwork *nn) {
    GameState state;
    char winner;
    int move_history[9]; // Maximum 9 moves in a game.
    int num_moves = 0;

    init_game(&state);

    printf("Welcome to Tic Tac Toe! You are X, the computer is O.\n");
    printf("Enter positions as numbers from 0 to 8 (see picture).\n");

    while (!check_game_over(&state, &winner)) {
        display_board(&state);

        if (state.current_player == 0) {
            // Human turn.
            int move;
            char movec;
            printf("Your move (0-8): ");
            scanf(" %c", &movec);
            move = movec-'0'; // Turn character into number.

            // Check if move is valid.
            if (move < 0 || move > 8 || state.board[move] != '.') {
                printf("Invalid move! Try again.\n");
                continue;
            }

            state.board[move] = 'X';
            move_history[num_moves++] = move;
        } else {
            // Computer's turn
            printf("Computer's move:\n");
            int move = get_computer_move(&state, nn, 1);
            state.board[move] = 'O';
            printf("Computer placed O at position %d\n", move);
            move_history[num_moves++] = move;
        }

        state.current_player = !state.current_player;
    }

    display_board(&state);

    if (winner == 'X') {
        printf("You win!\n");
    } else if (winner == 'O') {
        printf("Computer wins!\n");
    } else {
        printf("It's a tie!\n");
    }
}

/* Load neural network parameters from a file */
int load_neural_network(NeuralNetwork *nn, const char *filename) {
    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        printf("Error opening file for reading: %s\n", filename);
        return 1;
    }
    
    // Read weights and biases
    size_t items_read = 0;
    items_read += fread(nn->weights_ih, sizeof(float), NN_INPUT_SIZE * NN_HIDDEN_SIZE, file);
    items_read += fread(nn->weights_ho, sizeof(float), NN_HIDDEN_SIZE * NN_OUTPUT_SIZE, file);
    items_read += fread(nn->biases_h, sizeof(float), NN_HIDDEN_SIZE, file);
    items_read += fread(nn->biases_o, sizeof(float), NN_OUTPUT_SIZE, file);
    
    fclose(file);
    
    // Check if we read the expected number of items
    size_t expected_items = NN_INPUT_SIZE * NN_HIDDEN_SIZE + 
                           NN_HIDDEN_SIZE * NN_OUTPUT_SIZE + 
                           NN_HIDDEN_SIZE + 
                           NN_OUTPUT_SIZE;
    
    if (items_read != expected_items) {
        printf("Error: Read %zu items, expected %zu\n", items_read, expected_items);
        return 2;
    }
    
    printf("Neural network loaded from %s\n", filename);
    return 0;
}

int main(int argc, char **argv) {
    const char *input_file = "ttt_nn.bin";
    
    if (argc > 1) input_file = argv[1];
    
    // Load neural network from file
    NeuralNetwork nn;
    if (load_neural_network(&nn, input_file)) {
        printf("Failed to load neural network.\n");
        return 1;
    }

    printf("Ready to play! You are X, the computer is O.\n");
    
    // Play game with human
    while(1) {
        char play_again;
        play_game(&nn);

        printf("Play again? (y/n): ");
        scanf(" %c", &play_again);
        if (play_again != 'y' && play_again != 'Y') break;
    }
    return 0;
}
