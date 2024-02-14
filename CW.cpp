#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <iostream>
#include <string>
#include <cstdlib>
#include <condition_variable>

using namespace std;

std::chrono::steady_clock::time_point current_time = std::chrono::steady_clock::now();

void InitClock()
{
	current_time = std::chrono::steady_clock::now();
}

void PrintTime_ms(std::string text)
{
	std::chrono::steady_clock::time_point new_time = std::chrono::steady_clock::now();
	std::cout << text << std::chrono::duration_cast<std::chrono::milliseconds>(new_time - current_time).count() << " (ms)" << std::endl;
}

#define NB_ISLANDS 50 //number of islands
#define NB_BRIDGES 100 //number of bridges
#define NB_TAXIS 20 //number of taxis
#define NB_PEOPLE 40 //number of people per island

#define BRIDGE_CAPACITY 4 //number of taxis a bridge can hold

class Bridge;
class Person;
class Taxi;
class Island;
class Semaphore;

Bridge* bridges;
Taxi* taxis;
Island* islands;

class Semaphore
{
private:
	int N;
	mutex m;
	condition_variable cv;
public:
	Semaphore(int nb) { N = nb; };

	// Decrement semaphore count
	void P(int nb = 1) {
		std::unique_lock<std::mutex> lock(m);
		cv.wait(lock, [this, nb] { 
			//printf("%d resource(s) available, waiting for %d resource(s)...\n", N, nb);
			return N >= nb; 
		});
		N = N - nb;
		//printf("%d resource(s) captured, from %d/%d (required/available)\n", nb, nb, N+nb);
	};

	// Increment semaphore count
	void V(int nb = 1) {
		std::lock_guard<std::mutex> lock(m);
		N = N + nb;
		cv.notify_all();
	};
};


class Island
{
private:
	int nbPeople; //People that will take a taxi to travel somewhere
	int peopleDropped; //Total number of people dropped to the island
	std::mutex islandMutex;
public:
	int GetNbPeople() { return nbPeople; }
	int GetNbDroppedPeople() { return peopleDropped; }
	Island() { nbPeople = NB_PEOPLE; peopleDropped = 0; };

	// Pick up 1 passenger, returns if successful or not
	bool GetOnePassenger()
	{
		std::lock_guard<std::mutex> lock(islandMutex);
		if (GetNbPeople() <= 0) {
			return false;
		}
		nbPeople--;
		return true;
	}

	// Add one to tally of total people dropped
	void DropOnePassenger() 
	{
		std::lock_guard<std::mutex> lock(islandMutex);
		peopleDropped++;
		//printf("people dropped: %d, nb people: %d\n", GetNbDroppedPeople(), GetNbPeople());
	}
};


class Bridge
{
private:
	int origin, dest;
	Semaphore bridgeSemaphore; // Semaphore used to simulate lanes on the bridge
public:
	Bridge() : bridgeSemaphore(BRIDGE_CAPACITY)
	{
		origin = rand() % NB_ISLANDS;
		do
			dest = rand() % NB_ISLANDS;
		while (dest == origin);
	};
	int GetOrigin() { return origin; };
	int GetDest() { return dest; };
	void SetOrigin(int v) { origin = v; };
	void SetDest(int v) { dest = v; };

	Semaphore& GetBridgeSemaphore() {
		return this->bridgeSemaphore;
	}
};


class Taxi
{
private:
	int location; //island location
	int dest[4] = { -1,-1,-1,-1 }; //Destination of the people taken; -1 for seat is empty
	int GetId() { return this - taxis; }; //a hack to get the taxi thread id; Better would be to pass id throught the constructor
	std::mutex taxiMutex;
	
public:
	Taxi() { location = rand() % NB_ISLANDS; };

	void GetNewLocationAndBridge(int& location, int& bridge) 		//find a randomn bridge and returns the island on the other side;
	{
		int shift = rand() % NB_BRIDGES;
		for (int i = 0; i < NB_BRIDGES; i++)
		{
			if (bridges[(i + shift) % NB_BRIDGES].GetOrigin() == location)
			{
				location = bridges[(i + shift) % NB_BRIDGES].GetDest();
				bridge = (i + shift) % NB_BRIDGES;
				return;
			}
			if (bridges[(i + shift) % NB_BRIDGES].GetDest() == location)
			{
				location = bridges[(i + shift) % NB_BRIDGES].GetOrigin();
				bridge = (i + shift) % NB_BRIDGES;
				return;
			}
		}
	}

	void GetPassengers()
	{
		int cpt = 0;
		for (int i = 0; i < 4; i++)
			if ((dest[i] == -1) && (islands[location].GetOnePassenger()))
			{
				cpt++;
				do
					dest[i] = rand() % NB_ISLANDS;  //generating the destination for the individual randomly
				while (dest[i] == location);
			}
		if (cpt > 0)
			printf("Taxi %d has picked up %d clients on island %d.\n", GetId(), cpt, location);
	}

	void DropPassengers()
	{
		taxiMutex.lock();
		int cpt = 0;
		
		// Check if each person in the taxi has arrived at their destination
		// If so, drop person off at current island
		for (int i = 0; i < 4; i++) {
			if (dest[i] != -1) {  
				if (dest[i] == location) { 
					dest[i] = -1;
					islands[location].DropOnePassenger();
					cpt++;
				}
			}
		}
		if (cpt > 0)
			printf("Taxi %d has dropped %d clients on island %d.\n", GetId(), cpt, location);
		taxiMutex.unlock();
	}


	// Cross bridge regardless of taxi direction 
	void CrossBridge()
	{
		int bridge;
		GetNewLocationAndBridge(location, bridge);

		int origin = bridges[bridge].GetOrigin();
		int destination = bridges[bridge].GetDest();

		
		bridges[bridge].GetBridgeSemaphore().P();
		//printf("Taxi %d crossed bridge %d to island %d.\n", GetId(), bridge, location);
		bridges[bridge].GetBridgeSemaphore().V();
	}

	// Cross bridge only if the destination of taxis are the same (they're going the same direction)
	void CrossBridgeSameDirection()
	{
		int bridge;
		GetNewLocationAndBridge(location, bridge);

		int origin = bridges[bridge].GetOrigin();
		int destination = bridges[bridge].GetDest();

		if (location == destination) {
			bridges[bridge].GetBridgeSemaphore().P();
			//printf("Taxi %d crossed bridge %d to island %d.\n", GetId(), bridge, location);
			bridges[bridge].GetBridgeSemaphore().V();
		}
		
	}
};



/*	The "sum" variable:
*		"sum" is a local variable so it is not accessed externally 
*		and the variable does not need to be synchronised.
*		Additionally, "sum" is the only variable that is modified. 
*		
*		The "Island::GetNbDroppedPeople()" method is used to read data.
*		In this case, the returned value ("peopleDropped") is modified
*		while in mutual exclusion to avoid concurrency issues.
*		
*		The stopping condition is when the sum (of people dropped) is 
*		equal to the total number of people wanting to travel for
*		all islands.
*/
bool NotEnd()  //this function is already completed
{
	int sum = 0;
	for (int i = 0; i < NB_ISLANDS; i++)
		sum += islands[i].GetNbDroppedPeople();
	return sum != NB_PEOPLE * NB_ISLANDS;
}

void TaxiThread(int id)  //this function is already completed
{
	while (NotEnd())
	{
		taxis[id].GetPassengers();
		//taxis[id].CrossBridge();
		taxis[id].CrossBridgeSameDirection(); //Negligible difference in completion time with 4 but same direction
		taxis[id].DropPassengers();
	}
}

void RunTaxisUntilWorkIsDone()  //this function is already completed
{
	std::thread taxis[NB_TAXIS];
	for (int i = 0; i < NB_TAXIS; i++)
		taxis[i] = std::thread(TaxiThread, i);
	for (int i = 0; i < NB_TAXIS; i++)
		taxis[i].join();
}

//end of code for running taxis


void Init()
{
	bridges = new Bridge[NB_BRIDGES];
	for (int i = 0; i < NB_ISLANDS; i++) //Ensuring at least one path to all islands
	{
		bridges[i].SetOrigin(i);
		bridges[i].SetDest((i + 1) % NB_ISLANDS);
	}
	islands = new Island[NB_ISLANDS];
	taxis = new Taxi[NB_TAXIS];
}

void DeleteResources()
{
	delete[] bridges;
	delete[] taxis;
	delete[] islands;
}

int main(int argc, char* argv[])
{
	Init();
	InitClock();
	RunTaxisUntilWorkIsDone();
	printf("Taxis have completed!\n ");
	PrintTime_ms("Taxi time multithreaded:");
	DeleteResources();
	return 0;
}

