#ifndef BEAM_H
#define BEAM_H

#include <PalmOS.h>

void RegisterBeam(void);
void BeamAll(void);
Err ReceiveBeam(ExgSocketType *s, Boolean running);

#endif //BEAM_H
