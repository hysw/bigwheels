// Copyright 2023 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "ppx/knob.h"

namespace ppx {

// -------------------------------------------------------------------------------------------------
// KnobType
// -------------------------------------------------------------------------------------------------

std::ostream& operator<<(std::ostream& strm, KnobType kt)
{
    const std::string nameKnobType[] = {"Unknown"};
    return strm << nameKnobType[static_cast<int>(kt)];
}

} // namespace ppx
