#include "emonesp.h"
#include "lcd.h"
#include "RapiSender.h"

#define LCD_MAX_LEN 16

extern RapiSender rapiSender;

typedef struct Message_s Message;

struct Message_s
{
  Message *next;
  char msg[LCD_MAX_LEN];
  int x;
  int y;
  int time;
};

Message *head = NULL;
Message *tail = NULL;

bool lcdClaimed = false;
uint32_t nextTime = 0;

void lcd_display(Message *msg, int x, int y, int time, bool clear)
{
  if(clear) {
    for(int i = strlen(msg->msg); i < LCD_MAX_LEN; i++) {
      msg->msg[i] = '.';
    }
  }

  msg->x = x;
  msg->y = y;

  if(LCD_DISPLAY_NOW == time) {
    msg->time = 0;
    for(Message *next, *node = head; node; node = next) {
      next = node->next;
      delete node;
    }
    head = NULL;
    tail = NULL;
  } else {
    msg->time = time;
  }

  msg->next = NULL;
  if(tail) {
    tail->next = msg;
  } else {
    head = msg;
    nextTime = millis();
  }
  tail = msg;
}

void lcd_display(const __FlashStringHelper *msg, int x, int y, int time, bool clear)
{
  Message *msgStruct = new Message;
  strncpy_P(msgStruct->msg, reinterpret_cast<PGM_P>(msg), LCD_MAX_LEN);
  msgStruct->msg[LCD_MAX_LEN] = '\0';

  lcd_display(msgStruct, x, y, time, clear);
}

void lcd_display(String &msg, int x, int y, int time, bool clear)
{
  lcd_display(msg.c_str(), x, y, time, clear);
}

void lcd_display(const char *msg, int x, int y, int time, bool clear)
{
  Message *msgStruct = new Message;
  strncpy(msgStruct->msg, msg, LCD_MAX_LEN);
  msgStruct->msg[LCD_MAX_LEN] = '\0';
  lcd_display(msgStruct, x, y, time, clear);
}

void lcd_loop()
{
  if(millis() > nextTime)
  {
    if(head)
    {
      // Pop a message from the queue
      Message *msg = head;
      head = head->next;
      if(NULL == head) {
        tail = NULL;
      }

      // If the LCD has not been claimed, claim in
      if(false == lcdClaimed) {
        rapiSender.sendCmd(F("$F0 0"));
        lcdClaimed = true;
      }

      // Display the message
      String cmd = F("$FP ");
      cmd += msg->x;
      cmd += " ";
      cmd += msg->y;
      cmd += " ";
      cmd += msg->msg;
      rapiSender.sendCmd(cmd);

      nextTime = millis() + msg->time;

      // delete the message
      delete msg;
    }
    else if (lcdClaimed)
    {
      // No messages to display release the LCD.
      rapiSender.sendCmd(F("$F0 1"));
      lcdClaimed = false;
    }
  }
}

