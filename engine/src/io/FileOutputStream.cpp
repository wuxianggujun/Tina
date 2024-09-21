// Created by wuxianggujun on 2024/7/28.
//
#include "FileOutputStream.hpp"
#include <cstdio>


namespace Tina {
    FileOutputStream::FileOutputStream(const Path &path) {
        file_ = new File(path, Write | Binary);
        path_ = path;

        if (!file_->isOpen()) {
            throw std::runtime_error("Failed to open file for writing");
        }
    }

    FileOutputStream::FileOutputStream(File *file) {
        file_ = file;
        path_ = file->getPath();
        if (!file_->isOpen()) {
            throw std::runtime_error("File does not exist");
        }
    }

    FileOutputStream::~FileOutputStream() {
        FileOutputStream::close();
    }

    void FileOutputStream::write(const Byte &byte) {
        if (file_->isOpen()) {
            const uint8_t data = byte.getData();
            const size_t written = file_->getFileStream()->write(&data, sizeof(uint8_t), 1);
            if (written != sizeof(uint8_t)) {
                throw std::runtime_error("Failed to write byte to file");
            }
        }
    }

    void FileOutputStream::write(const Bytes &buffer) {
        if (file_->isOpen()) {
            if (const auto fileStream = file_->getFileStream()) {
                const auto *begin = reinterpret_cast<const uint8_t *>(buffer.begin());
                if (const size_t written = fileStream->write(begin, sizeof(uint8_t), buffer.size());
                    written != buffer.size()) {
                    throw std::runtime_error("Failed to write to file");
                }
            }
        }
    }

    void FileOutputStream::write(const Bytes &buffer, size_t size) {
        if (file_->isOpen()) {
            const auto *begin = reinterpret_cast<const uint8_t *>(buffer.begin());
            if (const auto fileStream = file_->getFileStream()) {
                if (const size_t written = fileStream->write(begin, sizeof(uint8_t), size);
                    written != size) {
                    throw std::runtime_error("Failed to write to file");
                }
            }
        }
    }

    void FileOutputStream::write(const Buffer<Byte> &buffer, size_t offset, size_t size) {
        if (file_->isOpen()) {
            if (const auto fileStream = file_->getFileStream()) {
                size_t written = fileStream->write(buffer.begin() + offset, sizeof(uint8_t), size);
                if (written != size) {
                    throw std::runtime_error("Failed to write to file");
                }
            }
        }
    }

    void FileOutputStream::close() {
        if (file_ && file_->isOpen()) {
            file_->close();
            delete file_;
            file_ = nullptr;
        }
    }

    void FileOutputStream::flush() {
        (void) file_->getFileStream()->flush();
    }

    void FileOutputStream::writeAndFlush(const Buffer<Byte> &buffer) {
        write(buffer);
        flush();
    }
} // Tina
