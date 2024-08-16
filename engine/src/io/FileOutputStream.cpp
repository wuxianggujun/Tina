// Created by wuxianggujun on 2024/7/28.
//
#include "FileOutputStream.hpp"
#include <cstdio>


namespace Tina {
    FileOutputStream::FileOutputStream(const Path &path) : path_(path), file_(new File(path, Write || Binary)) {
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
            file_->getFileStream()->write(reinterpret_cast<const uint8_t *>(byte.getData()), 1, sizeof(uint8_t));
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
                (void) fileStream->flush();
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
                (void) fileStream->flush();
            }
        }
    }

    void FileOutputStream::write(const Byte &byte, size_t offset, size_t size) {
        if (file_->isOpen()) {
            if (const auto fileStream = file_->getFileStream()) {
                fileStream->write(reinterpret_cast<const uint8_t *>(byte.getData()) + offset, sizeof(uint8_t), size);
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

    void FileOutputStream::writeAndFlush(const Byte &byte) {
        write(byte);
        flush(); 
    }
    
} // Tina
