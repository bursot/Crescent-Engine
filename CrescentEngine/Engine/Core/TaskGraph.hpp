#pragma once

#include "JobSystem.hpp"
#include <atomic>
#include <string>
#include <vector>

namespace Crescent {

class TaskGraph {
public:
    using TaskId = size_t;

    TaskId addTask(const std::string& name, JobSystem::Job task) {
        m_tasks.push_back(TaskDef{name, std::move(task), {}});
        return m_tasks.size() - 1;
    }

    void addDependency(TaskId task, TaskId dependsOn) {
        if (task >= m_tasks.size() || dependsOn >= m_tasks.size() || task == dependsOn) {
            return;
        }
        m_tasks[task].dependencies.push_back(dependsOn);
    }

    JobSystem::JobHandle run(JobSystem& jobSystem) const {
        auto state = std::make_shared<GraphState>();
        state->nodes.resize(m_tasks.size());

        for (size_t i = 0; i < m_tasks.size(); ++i) {
            state->nodes[i].task = m_tasks[i].task;
            state->nodes[i].pendingDeps.store(static_cast<int>(m_tasks[i].dependencies.size()),
                                              std::memory_order_relaxed);
        }

        for (size_t i = 0; i < m_tasks.size(); ++i) {
            for (TaskId dependency : m_tasks[i].dependencies) {
                if (dependency < state->nodes.size()) {
                    state->nodes[dependency].dependents.push_back(i);
                }
            }
        }

        JobSystem::JobHandle handle = jobSystem.createHandle();
        state->handle = handle;

        for (TaskId i = 0; i < state->nodes.size(); ++i) {
            if (state->nodes[i].pendingDeps.load(std::memory_order_relaxed) == 0) {
                scheduleTask(i, state, jobSystem);
            }
        }

        return handle;
    }

private:
    struct TaskDef {
        std::string name;
        JobSystem::Job task;
        std::vector<TaskId> dependencies;
    };

    struct GraphState {
        struct Node {
            JobSystem::Job task;
            std::vector<TaskId> dependents;
            std::atomic<int> pendingDeps{0};
            Node() = default;
            Node(const Node&) = delete;
            Node& operator=(const Node&) = delete;
            Node(Node&& other) noexcept
                : task(std::move(other.task))
                , dependents(std::move(other.dependents)) {
                pendingDeps.store(other.pendingDeps.load(std::memory_order_relaxed),
                                  std::memory_order_relaxed);
            }
            Node& operator=(Node&& other) noexcept {
                if (this != &other) {
                    task = std::move(other.task);
                    dependents = std::move(other.dependents);
                    pendingDeps.store(other.pendingDeps.load(std::memory_order_relaxed),
                                      std::memory_order_relaxed);
                }
                return *this;
            }
        };

        std::vector<Node> nodes;
        JobSystem::JobHandle handle;
    };

    static void scheduleTask(TaskId id,
                             const std::shared_ptr<GraphState>& state,
                             JobSystem& jobSystem) {
        jobSystem.submit([state, id, &jobSystem]() {
            if (state->nodes[id].task) {
                state->nodes[id].task();
            }
            for (TaskId dependent : state->nodes[id].dependents) {
                if (state->nodes[dependent].pendingDeps.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                    scheduleTask(dependent, state, jobSystem);
                }
            }
        }, state->handle);
    }

    std::vector<TaskDef> m_tasks;
};

} // namespace Crescent
