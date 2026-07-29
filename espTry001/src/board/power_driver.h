#pragma once

// Establishes the board's power-hold output before peripheral
// initialization. Boards without a power-hold circuit are a no-op.
void beginBoardPower();
