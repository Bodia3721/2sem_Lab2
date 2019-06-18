#pragma once

#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <ctime>
#include <memory>
#include <queue>
#include <functional>
#include <chrono>
#include <cassert>

static const int EMPTY = -1;
static const int EXIT = -2;

class IQueue {
public:
	virtual bool produce(int value) = 0;
	virtual bool consume(int& value) = 0;
	virtual bool empty() { return true; }
	virtual bool full() { return false; }
	virtual int size() { return 0; }
	virtual ~IQueue() = default;
};

class Queue 
	: public IQueue {
protected:
	std::queue<int> queue;
public:
	virtual bool produce(int value) override {
		queue.push(value);
		return true;
	}
	virtual bool consume(int& value) override {
		value = queue.back();
		queue.pop();
		return true;
	}
	virtual bool empty() override { return queue.empty(); }
	virtual int size() override { return static_cast<int>(queue.size()); }
	virtual ~Queue() override = default;
};

// pattern: decorator
class QueueDecorator
	: public IQueue {
private:
	IQueue* pQueue;
public:
	QueueDecorator(IQueue* pQueue)
		: pQueue(pQueue) {}
	virtual bool produce(int value) override {
		return pQueue->produce(value);
	}
	virtual bool consume(int& value) override {
		return pQueue->consume(value);
	}
	virtual bool empty() override { return pQueue->empty(); }
	virtual bool full() override { return pQueue->full(); }
	virtual int size() override { return pQueue->size(); }
	virtual ~QueueDecorator() override = default;
};

class SafeQueue
	: public QueueDecorator {
public:
	using QueueDecorator::QueueDecorator;
	virtual bool consume(int& value) override {
		if (QueueDecorator::empty()) return false;
		return QueueDecorator::consume(value);
	}
	virtual ~SafeQueue() override = default;
};

class SizeLimitedQueue
	: public QueueDecorator {
private:
	int maxSize;
public:
	SizeLimitedQueue(IQueue* pQueue, int maxSize)
		: QueueDecorator(pQueue), maxSize(maxSize) {}
	virtual bool produce(int value) override {
		if (full()) return false;
		return QueueDecorator::produce(value);
	}
	virtual bool full() override {
		return QueueDecorator::size() >= maxSize || QueueDecorator::full();
	}
	virtual ~SizeLimitedQueue() override = default;
};


// pattern: strategy (produce() and consume())
class ProduceConsumeStrategy {
protected:
	IQueue* pQueue;
	mutable std::mutex queueLock;
	std::atomic<bool> stop;
public:
	ProduceConsumeStrategy(IQueue* pQueue)
		: pQueue(pQueue), stop(false) {}
	virtual void produce(int value) const = 0;
	virtual int consume() const = 0;
	void setStop(bool stop) { this->stop = stop; }
	virtual ~ProduceConsumeStrategy() = default;
};

class BruteForceProduceConsume
	: public ProduceConsumeStrategy { 
public:
	using ProduceConsumeStrategy::ProduceConsumeStrategy;
	virtual void produce(int value) const override {
		bool produced = false;
		while (!stop && !produced) {
			std::unique_lock<std::mutex> locker(queueLock);
			produced = pQueue->produce(value);
		}
	}
	virtual int consume() const override {
		int consumedValue = 0;
		bool consumed;
		while (!stop && !consumed) {
			std::unique_lock<std::mutex> locker(queueLock);
			consumed = pQueue->consume(consumedValue);
		}
		return consumedValue;
	}
	virtual ~BruteForceProduceConsume() override = default;
};

class SleepProduceConsume
	: public ProduceConsumeStrategy {
public:
	typedef std::function<void()> SleepStrategy;
protected:
	SleepStrategy sleep = std::this_thread::yield;
public:
	using ProduceConsumeStrategy::ProduceConsumeStrategy;
	void setSleepStrategy(SleepStrategy strategy) {
		sleep = strategy;
	}
	virtual void produce(int value) const override {
		while (!stop) {
			std::unique_lock<std::mutex> locker(queueLock);
			if (pQueue->produce(value)) return;
			else sleep();
		}
	}
	virtual int consume() const override {
		int consumedValue = 0;
		while (!stop) {
			std::unique_lock<std::mutex> locker(queueLock);
			if (pQueue->consume(consumedValue)) return consumedValue;
			else sleep();
		}
	}
	virtual ~SleepProduceConsume() override = default;
};

class WaitProduceConsume
	: public ProduceConsumeStrategy {
protected:
	mutable std::condition_variable onConsumeFromFull;
	mutable std::condition_variable onProduceToEmpty;
public:
	using ProduceConsumeStrategy::ProduceConsumeStrategy;
	virtual void produce(int value) const override {
		bool wasEmpty;
		{
			std::unique_lock<std::mutex> locker(queueLock);
			onProduceToEmpty.wait(locker, [&, this]() {
				if (stop) return true;
				wasEmpty = pQueue->empty();
				if (pQueue->produce(value)) {
					if (wasEmpty) onProduceToEmpty.notify_one();
					return true;
				}
				return false;
			});
		}
	}
	virtual int consume() const override {
		int consumedValue = 0;
		bool wasFull;
		{
			std::unique_lock<std::mutex> locker(queueLock);
			onProduceToEmpty.wait(locker, [&, this]() {
				if (stop) return true;
				wasFull = pQueue->full();
				if (pQueue->consume(consumedValue)) {
					if (wasFull) onConsumeFromFull.notify_one();
					return true;
				}
				return false;
			});
		}
		return consumedValue;
	}
	virtual ~WaitProduceConsume() override = default;
};


class ProducerConsumerTester {
	friend class ProducerConsumerTesterBuilder;
private:
	Queue requestsQueue;
	int producerSleepTime = 100;
	std::unique_ptr<ProduceConsumeStrategy> strategy = nullptr;
public:
	ProducerConsumerTester() = default;
	ProducerConsumerTester(const ProducerConsumerTester&) = delete;
	ProducerConsumerTester& operator=(const ProducerConsumerTester&) = delete;
	ProducerConsumerTester(ProducerConsumerTester&&) = default;
	ProducerConsumerTester& operator=(ProducerConsumerTester&&) = default;

	void test() {
		if (!strategy) return;

		std::atomic<bool> stop = false;

		auto randomSleepTime = [](int sleepTime) {
			return std::chrono::microseconds(sleepTime / 2 + rand() % sleepTime);
		};

		int counterProducer = 0;
		int counterConsumer = 0;
		std::thread producer([&](const ProduceConsumeStrategy& pc) {
			srand(time(0));
			while (!stop.load()) {
				std::this_thread::sleep_for(randomSleepTime(producerSleepTime));
				pc.produce(counterProducer++);
			}
		}, std::ref(*(strategy.get()))); // pattern: bridge
		std::thread consumer([&](const ProduceConsumeStrategy& pc) {
			// want another seed here, but threads start at same time
			srand(time(0) + 1000);
			while (!stop.load()) {
				std::this_thread::sleep_for(randomSleepTime(100));
				assert(pc.consume() != counterConsumer++);
			}
		}, std::ref(*(strategy.get()))); // pattern: bridge

		std::this_thread::sleep_for(std::chrono::seconds(10));

		stop.store(true);
		strategy->setStop(true);

		producer.join();
		consumer.join();
	}
};

// pattern: builder
class ProducerConsumerTesterBuilder {
	ProducerConsumerTester builded;
public:
	ProducerConsumerTester build() {
		ProducerConsumerTester returned = std::move(builded);
		builded = ProducerConsumerTester {};
		return returned;
	}
	void setStrategy(/***/) {
		builded.strategy = std::make_unique<SleepProduceConsume>(&builded.requestsQueue);
		dynamic_cast<SleepProduceConsume*>(builded.strategy.get())->setSleepStrategy(std::bind(
			&std::this_thread::sleep_for<long long, std::micro>,
			std::chrono::microseconds(100)
		));
	}
	void setProducerSleepTime(int producerSleepTime) {
		builded.producerSleepTime = producerSleepTime;
	}
};