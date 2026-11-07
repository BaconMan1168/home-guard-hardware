#pragma once

#include "learn_session.h"
#include "stored_code.h"

void irLearnerBegin();
void irLearnerEnable(bool on);
bool irLearnerPoll(FrameInfo& frame, StoredCode& code);
