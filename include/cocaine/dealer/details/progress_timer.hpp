//
// Copyright (C) 2011-2012 Rim Zaidullin <creator@bash.org.ru>
//
// Licensed under the BSD 2-Clause License (the "License");
// you may not use this file except in compliance with the License.
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#ifndef _COCAINE_DEALER_PROGRESS_TIMER_HPP_INCLUDED_
#define _COCAINE_DEALER_PROGRESS_TIMER_HPP_INCLUDED_

#include "cocaine/dealer/details/time_value.hpp"

namespace cocaine {
namespace dealer {

class progress_timer {

public:
	progress_timer();
	virtual ~progress_timer();

	void reset();
	time_value elapsed();

private:
	time_value begin_;
};

} // namespace dealer
} // namespace cocaine

#endif // _COCAINE_DEALER_PROGRESS_TIMER_HPP_INCLUDED_