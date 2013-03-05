#include <ilias/combi_mq.h>

namespace ilias {

template class combiner<msg_queue<void>, msg_queue<int>, msg_queue<void>>;

}
