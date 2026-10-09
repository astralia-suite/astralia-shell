#pragma once

#include "modules/lock/wayland/lock.h"

struct Node;

void lock_card_build(LockState &st, LockOutputSurface &los, Node *root);
