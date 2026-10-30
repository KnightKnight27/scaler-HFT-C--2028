#ifndef SPSCQUEUE_H
#define SPSCQUEUE_H

#include <array>
#include <mutex>
#include <stddef.h>
#include <stdexcept>

template <typename T, size_t Capacity> class SPSCQueue {
public:
  SPSCQueue() = default;

  bool push(const T &val) {
    std::unique_lock<std::mutex> lock(m_mutex);
    if (full_nolock()) {
      return false;
    }

    if (empty_nolock()) {
      m_front = 0;
    }
    m_rear = next(m_rear);
    m_list[m_rear] = val;
    return true;
  }

  bool pop() {
    std::unique_lock<std::mutex> lock(m_mutex);
    if (empty_nolock()) {
      return false;
    }

    if (m_front == m_rear) {
      m_front = m_rear = -1;
    } else {
      m_front = next(m_front);
    }
    return true;
  }

  [[nodiscard]] T &front() {
    std::unique_lock<std::mutex> lock(m_mutex);
    if (empty_nolock()) {
      throw std::runtime_error("SPSCQueue: called front() on an empty queue");
    }
    return m_list[m_front];
  }

  [[nodiscard]] T &back() {
    std::unique_lock<std::mutex> lock(m_mutex);
    if (empty_nolock()) {
      throw std::runtime_error("SPSCQueue: called back() on an empty queue");
    }
    return m_list[m_rear];
  }

  [[nodiscard]] size_t size() const {
    std::unique_lock<std::mutex> lock(m_mutex);

    if (m_front == -1) {
      return 0;
    } else if (m_rear >= m_front) {
      return m_rear - m_front + 1;
    } else {
      return Capacity - m_front + m_rear + 1;
    }
  }

  [[nodiscard]] bool empty() const {
    std::unique_lock<std::mutex> lock(m_mutex);
    return (m_front == -1);
  }

  [[nodiscard]] bool full() const {
    std::unique_lock<std::mutex> lock(m_mutex);
    return (next(m_rear) == m_front);
  }

private:
  std::array<T, Capacity> m_list;
  int m_front{-1};
  int m_rear{-1};
  mutable std::mutex m_mutex;

  int next(int i) const { return ((i + 1) % Capacity); }

  [[nodiscard]] bool empty_nolock() const { return (m_front == -1); }

  [[nodiscard]] bool full_nolock() const { return (next(m_rear) == m_front); }
};

#endif // SPSCQUEUE_H
