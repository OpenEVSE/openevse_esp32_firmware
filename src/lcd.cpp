#include "emonesp.h"
#include "lcd.h"
#include "RapiSender.h"
#include "openevse.h"
#include "input.h"

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
  uint32_t clear:1;
};

Message *head = NULL;
Message *tail = NULL;

bool lcdClaimed = false;
uint32_t nextTime = 0;

void lcd_display(Message *msg, int x, int y, int time, uint32_t flags)
{
  msg->x = x;
  msg->y = y;
  msg->clear = flags & LCD_CLEAR_LINE ? 1 : 0;
  msg->time = time;

  if(flags & LCD_DISPLAY_NOW)
  {
    for(Message *next, *node = head; node; node = next) {
      next = node->next;
      delete node;
    }
    head = NULL;
    tail = NULL;
  }

  msg->next = NULL;
  if(tail) {
    tail->next = msg;
  } else {
    head = msg;
    nextTime = millis();
  }
  tail = msg;

  if(flags & LCD_DISPLAY_NOW) {
    lcd_loop();
  }
}

void lcd_display(const __FlashStringHelper *msg, int x, int y, int time, uint32_t flags)
{
  Message *msgStruct = new Message;
  strncpy_P(msgStruct->msg, reinterpret_cast<PGM_P>(msg), LCD_MAX_LEN);
  msgStruct->msg[LCD_MAX_LEN] = '\0';

  lcd_display(msgStruct, x, y, time, flags);
}

void lcd_display(String &msg, int x, int y, int time, uint32_t flags)
{
  lcd_display(msg.c_str(), x, y, time, flags);
}

void lcd_display(const char *msg, int x, int y, int time, uint32_t flags)
{
  Message *msgStruct = new Message;
  strncpy(msgStruct->msg, msg, LCD_MAX_LEN);
  msgStruct->msg[LCD_MAX_LEN] = '\0';
  lcd_display(msgStruct, x, y, time, flags);
}

void lcd_loop()
{
  // If the OpenEVSE has not started don't do anything
  if(OPENEVSE_STATE_STARTING == state) {
    return;
  }

  while(millis() >= nextTime)
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

      if(msg->clear)
      {
        for(int i = msg->x + strlen(msg->msg); i < LCD_MAX_LEN; i += 6)
        {
          // Older versions of the firmware crash if sending more than 6 spaces so clear the rest
          // of the line using blocks of 6 spaces
          String cmd = F("$FP ");
          cmd += i;
          cmd += " ";
          cmd += msg->y;
          cmd += "       "; // 7 spaces 1 separator and 6 to display
          rapiSender.sendCmd(cmd);
        }
      }

      nextTime = millis() + msg->time;

      // delete the message
      delete msg;
    }
    else if (lcdClaimed)
    {
      // No messages to display release the LCD.
      rapiSender.sendCmd(F("$F0 1"));
      lcdClaimed = false;
    } else {
      break;
    }
  }
}

