// GOAP System Implementation
//
// A* search over action space to find optimal plan.

#include "goap_system.h"
#include <queue>
#include <algorithm>
#include <iostream>
#include <string>

namespace goap {

void Planner::add_action(const Action& action) {
    actions_.push_back(action);
}

void Planner::clear_actions() {
    actions_.clear();
}

float Planner::heuristic(const WorldState& state, const Goal& goal) const {
    // Count unsatisfied goal conditions
    float count = 0;
    for (const auto& [key, value] : goal.desired_state) {
        auto it = state.find(key);
        if (it == state.end() || it->second != value) {
            count += 1.0f;
        }
    }
    return count;
}

Plan Planner::plan(const WorldState& current_state, const Goal& goal, int max_depth) {
    Plan result;
    result.valid = false;

    // Already satisfied?
    if (goal.is_satisfied(current_state)) {
        result.valid = true;
        result.total_cost = 0;
        if (verbose_) {
            std::cout << "[GOAP] Goal already satisfied" << std::endl;
        }
        return result;
    }

    // Priority queue: lowest f_cost first
    auto compare = [](const Node& a, const Node& b) {
        return a.f_cost() > b.f_cost();
    };
    std::priority_queue<Node, std::vector<Node>, decltype(compare)> open(compare);

    // Start node
    Node start;
    start.state = current_state;
    start.g_cost = 0;
    start.h_cost = heuristic(current_state, goal);
    open.push(start);

    // Track visited states (simple string hash)
    auto state_hash = [](const WorldState& s) {
        std::string h;
        for (const auto& [k, v] : s) {
            h += k + "=" + std::to_string(v) + ";";
        }
        return h;
    };
    std::unordered_map<std::string, float> visited;

    int iterations = 0;
    const int MAX_ITERATIONS = 1000;

    while (!open.empty() && iterations < MAX_ITERATIONS) {
        iterations++;

        Node current = open.top();
        open.pop();

        // Goal reached? Checked BEFORE the depth limit: a node holding a
        // complete plan of exactly max_depth actions is an ANSWER, and
        // the old order discarded it unexamined — a 2-action plan was
        // refused at max_depth=2 (#34). The depth limit exists to stop
        // EXPANSION, not to reject finished solutions.
        if (goal.is_satisfied(current.state)) {
            result.valid = true;
            result.actions = current.actions_taken;
            result.total_cost = current.g_cost;

            if (verbose_) {
                std::cout << "[GOAP] Plan found in " << iterations << " iterations:" << std::endl;
                for (const auto* action : result.actions) {
                    std::cout << "  -> " << action->name << " (cost " << action->cost << ")" << std::endl;
                }
                std::cout << "  Total cost: " << result.total_cost << std::endl;
            }
            return result;
        }

        // Depth limit gates EXPANSION only: nothing deeper is generated,
        // but a solution found AT the limit (above) was already accepted.
        if ((int)current.actions_taken.size() >= max_depth) {
            continue;
        }

        // Already visited with lower cost?
        std::string hash = state_hash(current.state);
        auto it = visited.find(hash);
        if (it != visited.end() && it->second <= current.g_cost) {
            continue;
        }
        visited[hash] = current.g_cost;

        // Try each action
        for (const Action& action : actions_) {
            if (!action.can_execute(current.state)) {
                continue;
            }

            // Create successor state
            Node successor;
            successor.state = current.state;
            apply_effects(successor.state, action.effects);

            successor.actions_taken = current.actions_taken;
            successor.actions_taken.push_back(&action);

            successor.g_cost = current.g_cost + action.get_cost(current.state);
            successor.h_cost = heuristic(successor.state, goal);

            open.push(successor);
        }
    }

    if (verbose_) {
        std::cout << "[GOAP] No plan found after " << iterations << " iterations" << std::endl;
    }

    return result;
}

} // namespace goap
