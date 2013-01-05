using namespace ilias;

const int min_prime = 2;
const int max_prime = 1000000;

int
main()
{
	wqs = new_workq_service();
	auto range = make_msgq_generator<int>(wqs->new_workq(),
	    min_prime, max_prime);
}
