Message queues
==============

A message queue is a concept used to facilitate asynchronous communication between multiple threads.  Messages are injected at one end of the queue and come out at the other end for processing.

Message queues implement concepts:
- *MessageQueue* as the base of the concept
- *MessageQueueRead* for reading from a message queue
- *MessageQueueWrite* for writing to a message queue

The ```class ilias::msg_queue<T, Alloc>``` implementation implements both the MessageQueueRead and MessageQueueWrite concepts.

When a message is written to a message queue, it describes a request to perform a function, with the message as argument.  For example, a web-browser could be implemented as a message queue, where each message to fetch a URL is pushed into a message queue and another comonent reads those messages and acts on them (displays a web-page).


MessageQueue
------------

This concept describes the general principles of a message queue.

Each message queue (class MQ) is thread-safe container of messages.  Messages enter the message queue on one end and exit on the other end.  Message queues do not preserve the input order on their output: ```mq.enqueue(a); mq.enqueue(b);``` may yield outputs ```a``` and ```b``` in any order.  Most message queues do use FIFO semantics or make a best attempt at maintaining them, but don't count on a specific ordering.

```typename MQ::element_type``` declares the type of the messages in the message queue.
```typename MQ::pointer```, ```typename MQ::const_pointer```, ```typename MQ::reference``` and ```typename MQ::const_reference``` respectively describe the pointer, const-pointer, reference and const-reference variant of ```element_type```.

Message queues cannot be copied, moved or assigned to (unless explicitly stated otherwise).


MessageQueueWrite
-----------------

A message queue implementing the *MessageQueueWrite* concept can be written to.

A single message is written to a message queue by calling the enqueue method.  The example write a message (42) to the message queue:

	ilias::msg_queue<int> mq;
	mq.enqueue(42);

The enqueue method is a template, which will cause the element type to be instantiated using the arguments provided.  For instance:

	ilias::msg_queue<std::tuple<int, int>> mq;
	mq.enqueue(6, 7);

will enqueue a single message ```std::tuple<int, int>{ 6, 7 }``` on the queue.

The enqueue method will provide the strong exception guarantee: in case an exception is thrown, the message queue will not be altered.

Additionally, a ```class prepare_enqueue<MQ>``` will be provided, which enable prepare-commit staging on inserted messages.  The commit method of this class will never throw, if prepare_enqueue is correctly used.  (I.e. it will throw if you call commit() without having actually prepared anything to be commited).


MessageQueueRead
----------------

This concept describes read capability on a message queue.  Each message that was enqueued on the message queue will be read at most once.

The read concept for a message queue ```MQ``` provides:

	bool MQ::empty() const noexcept;
	template<typename Functor> Functor MQ::dequeue(Functor f, std::size_t n = 1);

The ```empty()``` method can be used to test if the message queue has pending messages.  The ```dequeue()``` method can be used to read up to ```n``` messages from the queue, with each message being visited by the template Functor.

Furthermore, the MessageQueueRead aspect has two event callbacks:

	class MQ {
		friend void output_callback(MQ& mq, std::function<void()> callback) noexcept;
		friend void empty_callback(MQ& mq, std::function<void()> callback) noexcept;
		friend void output_callback(MQ& mq, std::nullptr_t) noexcept;
		friend void empty_callback(MQ& mq, std::nullptr_t) noexcept;
	};

Additional specializations may be provided for different callback implementations, which would be implemented in terms of the above.

The callback installed via ```output_callback``` is called whenever a new element is enqueued.  The empty callback is called when the message queue becomes empty (from a call to dequeue).  These calls may fire spuriously (i.e. the empty-callback is invoked while the message queue is not empty, or the output-callback is invoked while the message queue has no pending messages).

Example: a callback that appends messages to a global vector:

	ilias::msg_queue<int> my_msg_queue;
	std::vector<int> global_vector;

	int
	main()
	{
		output_callback(my_msg_queue, []() {
			my_msg_queue.dequeue([](int i) {
				global_vector.push_back(i);
			    }, SIZE_MAX);
		    });

		my_msg_queue.enqueue(42);
	}
