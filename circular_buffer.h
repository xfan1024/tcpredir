/*
 * Copyright (c) 2022, xiaofan <xfan1024@live.com>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef __circular_buffer_h__
#define __circular_buffer_h__

#include <stdio.h>  // for fprintf
#include <stdint.h> // for uint8_t

class circular_buffer {
public:
    circular_buffer(const circular_buffer&) = delete;
    circular_buffer& operator=(const circular_buffer&) = delete;

    circular_buffer() {
        _buffer = _w = _r = nullptr;
        _size = 0;
    }

    circular_buffer(size_t capacity) : circular_buffer(){
        allocate(capacity);
    }

    ~circular_buffer() {
        release();
    }

    void allocate(size_t capacity) {
        release();
        if (capacity) {
            _size = capacity + 1;
            _buffer = _w = _r = new uint8_t[_size];
        }
    }

    void release() {
        if (_buffer) {
            delete _buffer;
            _buffer = _w = _r = nullptr;
            _size = 0;
        }
    }

    size_t capacity(void) {
        if (bufsize()) {
            return bufsize() - 1;
        }
        return 0;
    }

    bool empty(void) {
        return used() == 0;
    }

    bool full(void) {
        return unused() == 0;
    }

    size_t used(void) {
        if (_w >= _r) {
            return _w - _r;
        }
        return bufsize() - (_r - _w);
    }

    size_t unused(void) {
        return capacity() - used();
    }

    uint8_t *readptr(size_t *max) {
        if (empty()) {
            *max = 0;
            return nullptr;
        }
        if (_w < _r) {
            *max = tail() - _r;
        } else {
            *max = _w - _r;
        }
        return _r;
    }

    bool consume(size_t len) {
        size_t limit;
        if (len == 0) {
            return true;
        }
        if (_w < _r) {
            limit = tail() - _r;
        } else {
            limit = _w - _r;
        }
        if (limit < len) {
            return false;
        }
        _r += len;
        if (_r == tail()) {
            _r = head();
        }
        // BUG: outside may store writeptr, set _w will lead to write wrong postition outside
        // if (_w == _r) {
        //     _w = _r = head();
        // }
        return true;
    }
    uint8_t *writeptr(size_t *max) {
        if (full()) {
            *max = 0;
            return nullptr;
        }
        if (_w >= _r) {
            *max = tail() - _w;
            if (head() == _r) {
                // disallow write to tail when _r point to head
                // if it allowed, _w == _r will happen, but it mean empty, not full.  
                *max -= 1; 
            }
        } else {
            *max = _r - _w - 1;
        }
        return _w;
    }

    bool produce(size_t len) {
        size_t limit;
        if (len == 0) {
            return true;
        }
        if (_w >= _r) {
            limit = tail() - _w;
            if (head() == _r) {
                // disallow write to tail when _r point to head
                // if it allowed, _w == _r will happen, but it mean empty, not full.  
                limit -= 1; 
            }
        } else {
            limit = _r - _w - 1;      
        }
        if (limit < len) {
            return false;
        }
        _w += len;
        if (_w == tail()) {
            _w = head();
        }
        return true;
    }

private:
    size_t bufsize() {
        return _size;
    }
    uint8_t* head() {
        return _buffer;
    }
    uint8_t* tail() {
        return head() + bufsize();
    }
private:
    uint8_t *_w, *_r;
    uint8_t *_buffer;
    size_t _size;
};

#endif
