// Copyright (c) Jared Taylor. All Rights Reserved


#include "TurnInPlaceTags.h"

namespace FTurnInPlaceTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(TurnMode, "TurnMode", "Used to lookup turn angle settings");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(TurnMode_Movement, "TurnMode.Movement", "Turn angle settings lookup to use when bOrientToMovement is true");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(TurnMode_Strafe, "TurnMode.Strafe", "Turn angle settings lookup to use when bOrientToMovement is false");
}