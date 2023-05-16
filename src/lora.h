/*
 * Copyright (c) 2019-2023 Alexander von Gluck IV for OpenEVSE
 *
 * -------------------------------------------------------------------
 *
 * Additional Adaptation of OpenEVSE ESP Wifi
 * by Trystan Lea, Glyn Hudson, OpenEnergyMonitor
 * All adaptation GNU General Public License as below.
 *
 * -------------------------------------------------------------------
 *
 * This file is part of Open EVSE.
 * Open EVSE is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 * Open EVSE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with Open EVSE; see the file COPYING.  If not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
#ifdef ENABLE_LORA
#ifndef _LORA_H
#define _LORA_H

#include <CayenneLPP.h>
#include <MicroTasks.h>

#include "evse_man.h"

void create_rapi_cayennelpp(EvseManager* _evse, CayenneLPP* lpp);

class LoraTask : public MicroTasks::Task {
  private:
        EvseManager*  _evse;

  protected:
        void          setup();
        unsigned long loop(MicroTasks::WakeReason reason);
        void          publish(CayenneLPP* lpp);
        void          modem_reset();

  public:
                      LoraTask();
        void          begin(EvseManager &evse);
};

extern LoraTask lora;

#endif // _LORA_H
#endif /* ENABLE_LORA */
