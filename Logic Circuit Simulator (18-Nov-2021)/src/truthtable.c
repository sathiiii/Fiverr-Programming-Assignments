#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_VARS    256

// Enumerated type for indicating the type of a variable (INPUT, OUTPUT, TEMPORARY, CONSTANT or DISCARDED)
typedef enum {
    INPUT,
    OUTPUT,
    TEMP,
    CONSTANT,
    DISCARDED
} var_type_t;

// Linked list node to store wire variables.
typedef struct _ {
    struct _ *next;
    struct _ *prev;
    char *key;  // Label for the wire
    int id;  // Mapping of the wire label to an integer
    var_type_t type;
} wire_t;

// Linked list to store the list of all wires.
// First set of wires are the inputs, second set are the outputs, and the rest are temporaries.
typedef struct {
    wire_t *head;
    wire_t *tail;
    int size;
    wire_t *map[MAX_VARS];
} wirelist_t;

// Enum for the type of a gate.
typedef enum { AND, OR, NAND, NOR, XOR, NOT, PASS, DECODER, MULTIPLEXER } type_t;

// Struct type to store the gate information.
typedef struct __ {
    type_t type;
    int size, id;
    int *params;    // Gates: First n params are inputs, last n params are outputs.
                    // Multiplexers: First 2^n params are inputs, followed by n selectors. Last n params are outputs.
                    // Decoders: First n params are inputs, followed by 2^n outputs.
    struct __ *next, *prev;
} gate_t;

// Linked list to store the list of gates.
typedef struct {
    int nGates;
    gate_t *head, *tail;
} gatelist_t;

// Struct type to store gate information to build the DAG of gates.
typedef struct ___ {
    int id;
    type_t type;
    int size;
    struct ___ **in, **out;
} gatenode_t;

// Struct type to store the DAG of gates.
typedef struct {
    gatenode_t **gatenodes;
    int nGatenodes;
} circuitbuilder_t;

// Struct type for stack data structure.
typedef struct {
    int top;
    int *stack;
} stack_t;


// Hash function for the wire label -> id mapping.
int hash(char *key) {
    int x = 31;
    int hash = 0;
    for (int i = strlen(key) - 1; i >= 0; i--) hash = (hash * x + key[i]) % MAX_VARS;
    return hash;
}

// Function to initialize the wire list.
void init_wirelist(wirelist_t *list) {
    list->head = NULL;
    list->tail = NULL;
    list->size = 0;
    for (int i = 0; i < MAX_VARS; i++) list->map[i] = NULL;
}

// Function to add a wire to the wire list.
wire_t *add_wire(wirelist_t *list, char *key, int id, var_type_t type) {
    wire_t *node = malloc(sizeof(wire_t));
    node->key = malloc(sizeof(char) * (strlen(key) + 1));
    strcpy(node->key, key);
    node->id = id;
    node->type = type;
    node->next = NULL;
    if (list->head == NULL) {
        list->head = node;
        list->tail = node;
        node->prev = NULL;
    } 
    else {
        node->prev = list->tail;
        list->tail->next = node;
        list->tail = node;
    }
    list->size++;
    return node;
}

// Function to map a wire label to an integer.
void hashtable_add(wirelist_t *list, char *key, int id, var_type_t type) {
    int hash_value = hash(key);
    if (list->map[hash_value] == NULL) {
        wire_t *node = add_wire(list, key, id, type);
        list->map[hash_value] = node;
    }
    else if (strcmp(list->map[hash_value]->key, key) != 0) {
        // Collision handling with quadratic probing
        int i = 1;
        while (list->map[(hash_value + i * i) % MAX_VARS] != NULL) i++;
        wire_t *node = add_wire(list, key, id, type);
        list->map[(hash_value + i * i) % MAX_VARS] = node;
    }
}

wire_t *hashtable_get(wirelist_t *list, int id) {
    for (int i = 0; i < MAX_VARS; i++) if (list->map[i] != NULL && list->map[i]->id == id) return list->map[i];
    return NULL;
}

wire_t *hashtable_get_by_key(wirelist_t *list, char *key) {
    int hash_value = hash(key);
    if (list->map[hash_value] == NULL) return NULL;
    else if (strcmp(list->map[hash_value]->key, key) == 0) return list->map[hash_value];
    else {
        // Collision handling with quadratic probing
        int i = 1;
        while (list->map[(hash_value + i * i) % MAX_VARS] != NULL && strcmp(list->map[(hash_value + i * i) % MAX_VARS]->key, key) != 0) i++;
        if (list->map[(hash_value + i * i) % MAX_VARS] == NULL) return NULL;
        else return list->map[(hash_value + i * i) % MAX_VARS];
    }
}

// Function to check if a wire is in the wire list.
int hashtable_contains(wirelist_t *list, char *key) {
    int hash_value = hash(key);
    if (list->map[hash_value] == NULL) return 0;
    else if (strcmp(list->map[hash_value]->key, key) == 0) return 1;
    else {
        // Collision handling with quadratic probing
        int i = 1;
        while (list->map[(hash_value + i * i) % MAX_VARS] != NULL && i < MAX_VARS + 1) {
            if (strcmp(list->map[(hash_value + i * i) % MAX_VARS]->key, key) == 0) return 1;
            i++;
        }
        return 0;
    }
}

// Function to initialize the gate list.
void init_circuit(gatelist_t *gates) {
    gates->nGates = 0;
    gates->head = NULL;
    gates->tail = NULL;
}

// Function to add a gate to the gate list.
void add_gate(gatelist_t *gates, type_t type, int size, int *params, int id) {
    gate_t *node = malloc(sizeof(gate_t));
    node->type = type;
    node->size = size;
    node->params = params;
    node->id = id;
    node->next = NULL;
    if (gates->head == NULL) {
        gates->head = node;
        gates->tail = node;
        node->prev = NULL;
    }
    else {
        node->prev = gates->tail;
        gates->tail->next = node;
        gates->tail = node;
    }
    gates->nGates++;
}

// Function to check if a variable is constant or discarded.
var_type_t check_type(char *param, var_type_t fallback_type) {
    if (strcmp(param, "0") == 0) return CONSTANT;
    if (strcmp(param, "1") == 0) return CONSTANT;
    if (strcmp(param, "_") == 0) return DISCARDED;
    return fallback_type;
}

// Function to calculate the number of inputs in a gate.
int calc_gate_in_size(gate_t *gate) {
    if (gate->type == MULTIPLEXER) return (1 << gate->size) + gate->size;
    return gate->size;
}

// Function to calculate the number of outputs in a gate.
int calc_gate_out_size(gate_t *gate) {
    if (gate->type == DECODER) return 1 << gate->size;
    return 1;
}

// Function to find the index of the gate that has the given wire as an output.
int get_gate_out_id(gatelist_t *gates, int wireId) {
    for (gate_t *gate = gates->head; gate != NULL; gate = gate->next)
        for (int i = calc_gate_in_size(gate); i < calc_gate_in_size(gate) + calc_gate_out_size(gate); i++)
            if (gate->params[i] == wireId) return gate->id;
    return -1;
}

// Function to find the index of the gate that has the given wire as an input.
int get_gate_in_id(gatelist_t *gates, int wireId) {
    for (gate_t *gate = gates->head; gate != NULL; gate = gate->next)
        for (int i = 0; i < calc_gate_in_size(gate); i++) if (gate->params[i] == wireId) return gate->id;
    return -1;
}

// Function to find the gate from the gate list that has the given id.
gate_t *get_gate(gatelist_t *gates, int id) {
    for (gate_t *gate = gates->head; gate != NULL; gate = gate->next) if (gate->id == id) return gate;
    return NULL;
}

// DFS to traverse the DAG of gates -> to find the topological order of the gates.
void dfs(int curr, int *visited, gatenode_t **gatenodes, gatelist_t *gates, stack_t *stack) {
    visited[curr] = 1;
    for (int i = 0; i < calc_gate_out_size(get_gate(gates, curr)); i++) {
        if (gatenodes[curr]->out[i] == NULL) continue;
        int next = gatenodes[curr]->out[i]->id;
        if (!visited[next]) dfs(next, visited, gatenodes, gates, stack);
    }
    stack->stack[stack->top++] = curr;
}

// Function to build the circuit and generate the truthtable for all possible inputs.
void build_circuit(wirelist_t *wires, gatelist_t *gates, circuitbuilder_t *builder, int nInputs, int nOutputs, int nWires) {
    builder->nGatenodes = gates->nGates;
    builder->gatenodes = malloc(sizeof(gatenode_t *) * gates->nGates);
    
    for (gate_t *gate = gates->head; gate != NULL; gate = gate->next) {
        builder->gatenodes[gate->id] = malloc(sizeof(gatenode_t));
        builder->gatenodes[gate->id]->id = gate->id;
        builder->gatenodes[gate->id]->type = gate->type;
        builder->gatenodes[gate->id]->size = gate->size;
        builder->gatenodes[gate->id]->in = malloc(sizeof(gatenode_t *) * calc_gate_in_size(gate));
        builder->gatenodes[gate->id]->out = malloc(sizeof(gatenode_t *) * calc_gate_out_size(gate));
    }

    for (gate_t *gate = gates->head; gate != NULL; gate = gate->next) {
        gatenode_t *node = builder->gatenodes[gate->id];
        for (int i = 0; i < calc_gate_in_size(gate); i++) {
            int wire_id = gate->params[i];
            wire_t *wire = hashtable_get(wires, wire_id);
            if (wire->type != CONSTANT && wire->type != INPUT) node->in[i] = builder->gatenodes[get_gate_out_id(gates, wire_id)];
            else node->in[i] = NULL;
        }
        for (int i = 0; i < calc_gate_out_size(gate); i++) {
            int wire_id = gate->params[calc_gate_in_size(gate) + i];
            wire_t *wire = hashtable_get(wires, wire_id);
            int id = get_gate_in_id(gates, wire_id);
            if (wire->type != DISCARDED && wire->type != OUTPUT && id != -1) node->out[i] = builder->gatenodes[id];
            else node->out[i] = NULL;
        }
        builder->gatenodes[gate->id] = node;
    }

    // Topologically sort the gates.
    int *visited = malloc(sizeof(int) * gates->nGates);
    for (int i = 0; i < gates->nGates; i++) visited[i] = 0;
    stack_t *stack = malloc(sizeof(stack_t));
    stack->top = 0;
    stack->stack = malloc(sizeof(int) * gates->nGates);
    for (int i = gates->nGates - 1; i >= 0; i--) if (!visited[i]) dfs(i, visited, builder->gatenodes, gates, stack);

    for (int in = 0; in < (1 << nInputs); in++) {
        int *inputs = malloc(sizeof(int) * nInputs);
        int *outputs = malloc(sizeof(int) * nOutputs);
        int *all_wires = malloc(sizeof(int) * nWires);

        for (int i = 0; i < nInputs; i++) inputs[nInputs - i - 1] = (in >> i) & 1;
    
        for (int i = gates->nGates - 1; i >= 0; i--) {
            gate_t *gate = get_gate(gates, stack->stack[i]);
            wire_t *wire;
            if (gate->type == AND) {
                all_wires[gate->params[2]] = 1;
                for (int j = 0; j < 2; j++) {
                    wire = hashtable_get(wires, gate->params[j]);
                    if (wire->type == INPUT) all_wires[wire->id] = inputs[wire->id];
                    else if (wire->type == CONSTANT) {
                        if (strcmp(wire->key, "0") == 0) all_wires[wire->id] = 0;
                        else if (strcmp(wire->key, "1") == 0) all_wires[wire->id] = 1;
                    }

                    // Output of the current gate.
                    all_wires[gate->params[2]] &= all_wires[gate->params[j]];
                }
                wire = hashtable_get(wires, gate->params[2]);
                if (wire->type == OUTPUT) outputs[wire->id - nInputs] = all_wires[gate->params[2]];
            }
            else if (gate->type == OR) {
                all_wires[gate->params[2]] = 0;
                for (int j = 0; j < 2; j++) {
                    wire = hashtable_get(wires, gate->params[j]);
                    if (wire->type == INPUT) all_wires[wire->id] = inputs[wire->id];
                    else if (wire->type == CONSTANT) {
                        if (strcmp(wire->key, "0") == 0) all_wires[wire->id] = 0;
                        else if (strcmp(wire->key, "1") == 0) all_wires[wire->id] = 1;
                    }

                    // Output of the current gate.
                    all_wires[gate->params[2]] |= all_wires[gate->params[j]];
                }
                wire = hashtable_get(wires, gate->params[2]);
                if (wire->type == OUTPUT) outputs[wire->id - nInputs] = all_wires[gate->params[2]];
            }
            else if (gate->type == NOT) {
                wire = hashtable_get(wires, gate->params[0]);
                if (wire->type == INPUT) all_wires[wire->id] = inputs[wire->id];
                else if (wire->type == CONSTANT) {
                    if (strcmp(wire->key, "0") == 0) all_wires[wire->id] = 0;
                    else if (strcmp(wire->key, "1") == 0) all_wires[wire->id] = 1;
                }

                // Output of the current gate.
                all_wires[gate->params[1]] = !all_wires[gate->params[0]];
                wire = hashtable_get(wires, gate->params[1]);
                if (wire->type == OUTPUT) outputs[wire->id - nInputs] = all_wires[gate->params[1]];
            }
            else if (gate->type == NAND) {
                all_wires[gate->params[2]] = 1;
                for (int j = 0; j < 2; j++) {
                    wire = hashtable_get(wires, gate->params[j]);
                    if (wire->type == INPUT) all_wires[wire->id] = inputs[wire->id];
                    else if (wire->type == CONSTANT) {
                        if (strcmp(wire->key, "0") == 0) all_wires[wire->id] = 0;
                        else if (strcmp(wire->key, "1") == 0) all_wires[wire->id] = 1;
                    }

                    // Output of the current gate.
                    all_wires[gate->params[2]] &= all_wires[gate->params[j]];
                }
                all_wires[gate->params[2]] = !all_wires[gate->params[2]];
                wire = hashtable_get(wires, gate->params[2]);
                if (wire->type == OUTPUT) outputs[wire->id - nInputs] = all_wires[gate->params[2]];
            }
            else if (gate->type == NOR) {
                all_wires[gate->params[2]] = 0;
                for (int j = 0; j < 2; j++) {
                    wire = hashtable_get(wires, gate->params[j]);
                    if (wire->type == INPUT) all_wires[wire->id] = inputs[wire->id];
                    else if (wire->type == CONSTANT) {
                        if (strcmp(wire->key, "0") == 0) all_wires[wire->id] = 0;
                        else if (strcmp(wire->key, "1") == 0) all_wires[wire->id] = 1;
                    }

                    // Output of the current gate.
                    all_wires[gate->params[2]] |= all_wires[gate->params[j]];
                }
                all_wires[gate->params[2]] = !all_wires[gate->params[2]];
                wire = hashtable_get(wires, gate->params[2]);
                if (wire->type == OUTPUT) outputs[wire->id - nInputs] = all_wires[gate->params[2]];
            }
            else if (gate->type == XOR) {
                all_wires[gate->params[2]] = 0;
                for (int j = 0; j < 2; j++) {
                    wire = hashtable_get(wires, gate->params[j]);
                    if (wire->type == INPUT) all_wires[wire->id] = inputs[wire->id];
                    else if (wire->type == CONSTANT) {
                        if (strcmp(wire->key, "0") == 0) all_wires[wire->id] = 0;
                        else if (strcmp(wire->key, "1") == 0) all_wires[wire->id] = 1;
                    }

                    // Output of the current gate.
                    all_wires[gate->params[2]] ^= all_wires[gate->params[j]];
                }
                wire = hashtable_get(wires, gate->params[2]);
                if (wire->type == OUTPUT) outputs[wire->id - nInputs] = all_wires[gate->params[2]];
            }
            else if (gate->type == PASS) {
                wire = hashtable_get(wires, gate->params[0]);
                if (wire->type == INPUT) all_wires[wire->id] = inputs[wire->id];
                else if (wire->type == CONSTANT) {
                    if (strcmp(wire->key, "0") == 0) all_wires[wire->id] = 0;
                    else if (strcmp(wire->key, "1") == 0) all_wires[wire->id] = 1;
                }

                // Output of the current gate.
                all_wires[gate->params[1]] = all_wires[gate->params[0]];
                wire = hashtable_get(wires, gate->params[1]);
                if (wire->type == OUTPUT) outputs[wire->id - nInputs] = all_wires[gate->params[1]];
            }
            else if (gate->type == DECODER) {
                int decoder_output = 0;
                for (int j = 0; j < gate->size; j++) {
                    wire = hashtable_get(wires, gate->params[j]);
                    if (wire->type == INPUT) all_wires[wire->id] = inputs[wire->id];
                    else if (wire->type == CONSTANT) {
                        if (strcmp(wire->key, "0") == 0) all_wires[wire->id] = 0;
                        else if (strcmp(wire->key, "1") == 0) all_wires[wire->id] = 1;
                    }

                    // Output of the current gate.
                    decoder_output |= all_wires[gate->params[j]] << (gate->size - j - 1);
                }

                for (int j = calc_gate_in_size(gate); j < calc_gate_in_size(gate) + calc_gate_out_size(gate); j++) {
                    wire = hashtable_get(wires, gate->params[j]);
                    all_wires[gate->params[j]] = (j - calc_gate_in_size(gate)) == decoder_output;
                    if (wire->type == OUTPUT) outputs[wire->id - nInputs] = all_wires[gate->params[j]];
                }
            }
            else if (gate->type == MULTIPLEXER) {
                int selector = 0;
                for (int i = 0; i < (1 << gate->size); i++) {
                    wire = hashtable_get(wires, gate->params[i]);
                    if (wire->type == INPUT) all_wires[wire->id] = inputs[wire->id];
                    else if (wire->type == CONSTANT) {
                        if (strcmp(wire->key, "0") == 0) all_wires[wire->id] = 0;
                        else if (strcmp(wire->key, "1") == 0) all_wires[wire->id] = 1;
                    }
                }
                for (int i = (1 << gate->size); i < calc_gate_in_size(gate); i++) {
                    wire = hashtable_get(wires, gate->params[i]);
                    if (wire->type == INPUT) all_wires[wire->id] = inputs[wire->id];
                    else if (wire->type == CONSTANT) {
                        if (strcmp(wire->key, "0") == 0) all_wires[wire->id] = 0;
                        else if (strcmp(wire->key, "1") == 0) all_wires[wire->id] = 1;
                    }

                    selector |= all_wires[gate->params[i]] << (calc_gate_in_size(gate) - i - 1);
                }

                // Output of the current gate.
                all_wires[gate->params[calc_gate_in_size(gate)]] = all_wires[gate->params[selector]];
                wire = hashtable_get(wires, gate->params[calc_gate_in_size(gate)]);
                if (wire->type == OUTPUT) outputs[wire->id - nInputs] = all_wires[gate->params[calc_gate_in_size(gate)]];
            }
        }

        // for (wire_t *wire = wires->head; wire != NULL; wire = wire->next) printf("%s: %d\n", wire->key, all_wires[wire->id]);

        for (int i = 0; i < nInputs; i++) printf("%d ", inputs[i]);
        printf("| ");
        for (int i = 0; i < nOutputs; i++) {
            printf("%d", outputs[i]);
            if (i != nOutputs - 1) printf(" ");
        }
        printf("\n");

        free(inputs);
        free(outputs);
        free(all_wires);
    }

    // Debugging.
    // for (int i = builder->nGatenodes - 1; i >= 0 ; i--) printf("%d ", stack->stack[i]);
    // printf("\n");

    free(visited);
    free(stack->stack);
    free(stack);
}

// Function to parse the input circuit description file.
int parse_circuit(FILE *fp, wirelist_t *wires, gatelist_t *gates, int *nInputs, int *nOutputs, int *gateId, int *wireId) {
    char token[17];

    while (fscanf(fp, " %16s", token) == 1) {
        if (strcmp(token, "END") == 0) return 0;

        if (strcmp(token, "INPUT") == 0) {
            fscanf(fp, " %d", nInputs);
            for (int i = 0; i < *nInputs; i++) {
                fscanf(fp, " %16s", token);
                if (!hashtable_contains(wires, token)) hashtable_add(wires, token, (*wireId)++, check_type(token, INPUT));
            }
        }
        else if (strcmp(token, "OUTPUT") == 0) {
            fscanf(fp, " %d", nOutputs);
            for (int i = 0; i < *nOutputs; i++) {
                fscanf(fp, " %16s", token);
                if (!hashtable_contains(wires, token)) hashtable_add(wires, token, (*wireId)++, OUTPUT);
            }
        }
        else if (strcmp(token, "AND") == 0) {
            int *params = malloc(sizeof(int) * 3);
            fscanf(fp, " %16s", token);
            if (!hashtable_contains(wires, token)) hashtable_add(wires, token, (*wireId)++, check_type(token, TEMP));
            params[0] = hashtable_get_by_key(wires, token)->id;
            fscanf(fp, " %16s", token);
            if (!hashtable_contains(wires, token)) hashtable_add(wires, token, (*wireId)++, check_type(token, TEMP));
            params[1] = hashtable_get_by_key(wires, token)->id;
            fscanf(fp, " %16s", token);
            if (!hashtable_contains(wires, token)) hashtable_add(wires, token, (*wireId)++, check_type(token, TEMP));
            params[2] = hashtable_get_by_key(wires, token)->id;

            add_gate(gates, AND, 2, params, (*gateId)++);
        }
        else if (strcmp(token, "OR") == 0) {
            int *params = malloc(sizeof(int) * 3);
            fscanf(fp, " %16s", token);
            if (!hashtable_contains(wires, token)) hashtable_add(wires, token, (*wireId)++, check_type(token, TEMP));
            params[0] = hashtable_get_by_key(wires, token)->id;
            fscanf(fp, " %16s", token);
            if (!hashtable_contains(wires, token)) hashtable_add(wires, token, (*wireId)++, check_type(token, TEMP));
            params[1] = hashtable_get_by_key(wires, token)->id;
            fscanf(fp, " %16s", token);
            if (!hashtable_contains(wires, token)) hashtable_add(wires, token, (*wireId)++, check_type(token, TEMP));
            params[2] = hashtable_get_by_key(wires, token)->id;

            add_gate(gates, OR, 2, params, (*gateId)++);
        }
        else if (strcmp(token, "NAND") == 0) {
            int *params = malloc(sizeof(int) * 3);
            fscanf(fp, " %16s", token);
            if (!hashtable_contains(wires, token)) hashtable_add(wires, token, (*wireId)++, check_type(token, TEMP));
            params[0] = hashtable_get_by_key(wires, token)->id;
            fscanf(fp, " %16s", token);
            if (!hashtable_contains(wires, token)) hashtable_add(wires, token, (*wireId)++, check_type(token, TEMP));
            params[1] = hashtable_get_by_key(wires, token)->id;
            fscanf(fp, " %16s", token);
            if (!hashtable_contains(wires, token)) hashtable_add(wires, token, (*wireId)++, check_type(token, TEMP));
            params[2] = hashtable_get_by_key(wires, token)->id;

            add_gate(gates, NAND, 2, params, (*gateId)++);
        }
        else if (strcmp(token, "NOR") == 0) {
            int *params = malloc(sizeof(int) * 3);
            fscanf(fp, " %16s", token);
            if (!hashtable_contains(wires, token)) hashtable_add(wires, token, (*wireId)++, check_type(token, TEMP));
            params[0] = hashtable_get_by_key(wires, token)->id;
            fscanf(fp, " %16s", token);
            if (!hashtable_contains(wires, token)) hashtable_add(wires, token, (*wireId)++, check_type(token, TEMP));
            params[1] = hashtable_get_by_key(wires, token)->id;
            fscanf(fp, " %16s", token);
            if (!hashtable_contains(wires, token)) hashtable_add(wires, token, (*wireId)++, check_type(token, TEMP));
            params[2] = hashtable_get_by_key(wires, token)->id;

            add_gate(gates, NOR, 2, params, (*gateId)++);
        }
        else if (strcmp(token, "XOR") == 0) {
            int *params = malloc(sizeof(int) * 3);
            fscanf(fp, " %16s", token);
            if (!hashtable_contains(wires, token)) hashtable_add(wires, token, (*wireId)++, check_type(token, TEMP));
            params[0] = hashtable_get_by_key(wires, token)->id;
            fscanf(fp, " %16s", token);
            if (!hashtable_contains(wires, token)) hashtable_add(wires, token, (*wireId)++, check_type(token, TEMP));
            params[1] = hashtable_get_by_key(wires, token)->id;
            fscanf(fp, " %16s", token);
            if (!hashtable_contains(wires, token)) hashtable_add(wires, token, (*wireId)++, check_type(token, TEMP));
            params[2] = hashtable_get_by_key(wires, token)->id;

            add_gate(gates, XOR, 2, params, (*gateId)++);
        }
        else if (strcmp(token, "NOT") == 0) {
            int *params = malloc(sizeof(int) * 3);
            fscanf(fp, " %16s", token);
            if (!hashtable_contains(wires, token)) hashtable_add(wires, token, (*wireId)++, check_type(token, TEMP));
            params[0] = hashtable_get_by_key(wires, token)->id;
            fscanf(fp, " %16s", token);
            if (!hashtable_contains(wires, token)) hashtable_add(wires, token, (*wireId)++, check_type(token, TEMP));
            params[1] = hashtable_get_by_key(wires, token)->id;

            add_gate(gates, NOT, 1, params, (*gateId)++);
        }
        else if (strcmp(token, "PASS") == 0) {
            int *params = malloc(sizeof(int) * 3);
            fscanf(fp, " %16s", token);
            if (!hashtable_contains(wires, token)) hashtable_add(wires, token, (*wireId)++, check_type(token, TEMP));
            params[0] = hashtable_get_by_key(wires, token)->id;
            fscanf(fp, " %16s", token);
            if (!hashtable_contains(wires, token)) hashtable_add(wires, token, (*wireId)++, check_type(token, TEMP));
            params[1] = hashtable_get_by_key(wires, token)->id;

            add_gate(gates, PASS, 1, params, (*gateId)++);
        }
        else if (strcmp(token, "DECODER") == 0) {
            fscanf(fp, " %16s", token);
            int n = atoi(token);
            int *params = malloc(sizeof(int) * ((1 << n) + n));
            for (int i = 0; i < n; i++) {
                fscanf(fp, " %16s", token);
                if (!hashtable_contains(wires, token)) hashtable_add(wires, token, (*wireId)++, check_type(token, TEMP));
                params[i] = hashtable_get_by_key(wires, token)->id;
            }
            for (int i = 0; i < (1 << n); i++) {
                fscanf(fp, " %16s", token);
                if (!hashtable_contains(wires, token)) hashtable_add(wires, token, (*wireId)++, check_type(token, TEMP));
                params[i + n] = hashtable_get_by_key(wires, token)->id;
            }

            add_gate(gates, DECODER, n, params, (*gateId)++);
        }
        else if (strcmp(token, "MULTIPLEXER") == 0) {
            fscanf(fp, " %16s", token);
            int n = atoi(token);
            int *params = malloc(sizeof(int) * ((1 << n) + n + 1));
            for (int i = 0; i < (1 << n) + n + 1; i++) {
                fscanf(fp, " %16s", token);
                if (!hashtable_contains(wires, token)) hashtable_add(wires, token, (*wireId)++, check_type(token, TEMP));
                params[i] = hashtable_get_by_key(wires, token)->id;
            }

            add_gate(gates, MULTIPLEXER, n, params, (*gateId)++);
        }
        else {
            printf("Error: Unknown token %s\n", token);
            return 1;
        }
    }

    return 0;
}

int main(int argc, char const *argv[]) {
    int nInputs, nOutputs, gateId = 0, wireId = 0;
    wirelist_t wires;
    gatelist_t gates;
    circuitbuilder_t builder;

    init_wirelist(&wires);
    init_circuit(&gates);

    if (argc == 1) {
        int res = parse_circuit(stdin, &wires, &gates, &nInputs, &nOutputs, &gateId, &wireId);
        if (res) return 1;

        build_circuit(&wires, &gates, &builder, nInputs, nOutputs, wireId);
    }
    else if (argc == 2) {
        FILE *fp = fopen(argv[1], "r");
        if (fp == NULL) {
            printf("Error opening file\n");
            return 1;
        }

        int res = parse_circuit(fp, &wires, &gates, &nInputs, &nOutputs, &gateId, &wireId);
        if (res) return 1;

        fclose(fp);

        // Debugging.
        // for (wire_t *wire = wires.head; wire != NULL; wire = wire->next) printf("%s: %d ", wire->key, wire->id);
        // printf("\n");

        build_circuit(&wires, &gates, &builder, nInputs, nOutputs, wireId);
    }
    else {
        printf("Too many arguments.\n");
        return 1;
    }

    // Free memory from gatelist
    gate_t *g = gates.tail;
    while (g != NULL) {
        g = g->prev;
        free(gates.tail->params);
        free(gates.tail);
        gates.tail = g;
    }

    // Free memory from wirelist
    wire_t *node = wires.tail;
    while (node != NULL) {
        node = node->prev;
        free(wires.tail->key);
        free(wires.tail);
        wires.tail = node;
    }

    // Free memory from circuitbuilder
    for (int i = 0; i < builder.nGatenodes ; i++) {
        free(builder.gatenodes[i]->in);
        free(builder.gatenodes[i]->out);
        free(builder.gatenodes[i]);
    }
    free(builder.gatenodes);

    return 0;
}
