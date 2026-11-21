#pragma once
#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

namespace GakuMotion {
    // The Unity thread submits formatted batches. Only the writer owns the
    // open file. Close drains accepted batches and reports any write failure.
    class CaptureFileWriter {
        static constexpr size_t kPendingLimit = 4u*1024u*1024u;
        std::thread worker_;
        std::mutex mutex_;
        std::condition_variable ready_;
        std::deque<std::string> batches_;
        size_t queuedBytes_{};
        bool closing_{};
        std::atomic<bool> good_{false};

        void Run(std::ofstream file)
        {
            for (;;) {
                std::string batch;
                {
                    std::unique_lock lock(mutex_);
                    ready_.wait(lock,[&] { return closing_ || !batches_.empty(); });
                    if (batches_.empty()) break;
                    batch = std::move(batches_.front());
                    queuedBytes_ -= batch.size();
                    batches_.pop_front();
                }
                if (file) {
                    file.write(batch.data(),static_cast<std::streamsize>(batch.size()));
                    file.flush();
                }
                if (!file) good_.store(false);
            }
            file.close();
            if (!file) good_.store(false);
        }

    public:
        ~CaptureFileWriter() { Close(); }
        bool Open(const std::filesystem::path& path)
        {
            if (Opened()) return false;
            std::ofstream file(path,std::ios::binary);
            if (!file) return false;
            closing_ = false;
            good_.store(true);
            try { worker_ = std::thread([this,file=std::move(file)]() mutable { Run(std::move(file)); }); }
            catch (...) { good_.store(false); return false; }
            return true;
        }

        bool Opened() const { return worker_.joinable(); }
        bool Good() const { return good_.load(); }

        bool Submit(std::string batch)
        {
            if (!Good()) return false;
            if (batch.empty()) return true;
            {
                std::lock_guard lock(mutex_);
                if (batch.size() > kPendingLimit-queuedBytes_) {
                    good_.store(false);
                    return false;
                }
                const auto size = batch.size();
                batches_.push_back(std::move(batch));
                queuedBytes_ += size;
            }
            ready_.notify_one();
            return true;
        }

        bool Close()
        {
            if (!Opened()) return false;
            {
                std::lock_guard lock(mutex_);
                closing_ = true;
            }
            ready_.notify_one();
            worker_.join();
            return Good();
        }
    };
}
