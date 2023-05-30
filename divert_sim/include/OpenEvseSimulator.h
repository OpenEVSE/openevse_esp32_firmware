#ifndef OPEN_EVSE_SIMULATOR_H
#define OPEN_EVSE_SIMULATOR_H

#include <Arduino.h>

#include "MicroTasks.h"
#include <queue>
#include <string>
#include <iostream>
#include <sstream>

class OpenEvseSimulator : public MicroTasks::Task
{
  private:
    class SimulatorStream : public Stream
    {
      private:
        std::stringbuf _in_buffer;
        std::stringbuf _out_buffer;

      public:
        SimulatorStream() :
          _out_buffer()
        {
        }

        void begin(size_t buffer_size) {
        }
        void end() {
        }

        int available(void) {
          return _out_buffer.in_avail();
        }

        int peek(void) {
          return _out_buffer.sgetc();
        }
        int read(void) {
          return _out_buffer.sbumpc();
        }
        void flush(void) {
        }
        size_t write(uint8_t chr) {
          return _in_buffer.sputc(chr);
        }
        size_t write(const uint8_t *buffer, size_t size) {
          return _in_buffer.sputn((const char *) buffer, size);
        }

        inline size_t write(const char * s)
        {
          return write((uint8_t*) s, strlen(s));
        }
        inline size_t write(unsigned long n)
        {
          return write((uint8_t) n);
        }
        inline size_t write(long n)
        {
          return write((uint8_t) n);
        }
        inline size_t write(unsigned int n)
        {
          return write((uint8_t) n);
        }
        inline size_t write(int n)
        {
          return write((uint8_t) n);
        }

        bool internalAvailable()
        {
          return _in_buffer.in_avail() > 0;
        }

        std::string internalRead()
        {
          std::string str;
          std::string buffer = _in_buffer.str();
          std::getline(std::istringstream(buffer), str, '\r');
          buffer.erase(0, str.length() + 1);
          _in_buffer.str(buffer);
          return str;
        }

        void internalWrite(std::string &str)
        {
          _out_buffer.sputn(str.c_str(), str.length());
        }
    };

    std::queue<std::string> _replyQueue;
    SimulatorStream _stream;

    long _pilot;                       // OpenEVSE Pilot Setting
    long _state; // OpenEVSE State

  protected:
    void setup() {
    }

    unsigned long loop(MicroTasks::WakeReason reason);

  public:

    OpenEvseSimulator();

    void begin() {
      MicroTask.startTask(this);
    }

    Stream &stream() {
      return _stream;
    }

    long pilot() {
      return _pilot;
    }

    long state() {
      return _state;
    }

  private:
    void processCommand(std::string &cmd);
    void sendReply(const char *reply);
};

#endif // __OPENEVSESIMULATOR_H
