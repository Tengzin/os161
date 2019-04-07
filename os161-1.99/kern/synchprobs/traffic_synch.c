#include <types.h>
#include <lib.h>
#include <synchprobs.h>
#include <synch.h>
#include <opt-A1.h>
#include <array.h>

/* 
 * This simple default synchronization mechanism allows only vehicle at a time
 * into the intersection.   The intersectionSem is used as a a lock.
 * We use a semaphore rather than a lock so that this code will work even
 * before locks are implemented.
 */

/* Currently tries to use 1 lock and 4 cvs, each cv for a direction

*/

typedef struct Car {
  Direction origin;
  Direction destination;
} Car;

struct array* car_list;

bool canRightTurn(Car *car);
bool canEnter(Car *incoming, Car *here);
bool safeToGo(Car *car);

// static struct semaphore *intersectionSem;

static struct lock *intersectionLock;

static struct cv *north_cv;
static struct cv *south_cv;
static struct cv *east_cv;
static struct cv *west_cv;

/* 
 * The simulation driver will call this function once before starting
 * the simulation
 *
 * You can use it to initialize synchronization and other variables.
 * 
 */
void
intersection_sync_init(void)
{
  // kprintf("Initiating protocol:\n");

  north_cv = cv_create("north_cv");
  
  /* replace this default implementation with your own implementation */
  intersectionLock = lock_create("intersectionLock");
  
  car_list = array_create();
  array_init(car_list);

  if(north_cv == NULL) {
    panic("could not create north_cv");
  }
  if (intersectionLock == NULL) {
    panic("could not create intersection lock");
  }
  south_cv = cv_create("southcv");
  if(south_cv == NULL) {
    panic("could not create south_cv");
  }
  east_cv = cv_create("eastcv");
  if(east_cv == NULL) {
    panic("could not create east_cv");
  }
  west_cv = cv_create("westcv");
  if(west_cv == NULL) {
    panic("could not create west_cv");
  }

  // intersectionSem = sem_create("intersectionSem",1);
  // if (intersectionSem == NULL) {
  //   panic("could not create intersection semaphore");
  // }
  return;
}

/* 
 * The simulation driver will call this function once after
 * the simulation has finished
 *
 * You can use it to clean up any synchronization and other variables.
 *
 */
void intersection_sync_cleanup(void)
{
  // kprintf("cleaning up...\n");
  KASSERT(intersectionLock != NULL);
  KASSERT(north_cv != NULL);
  KASSERT(car_list != NULL);
  KASSERT(south_cv != NULL);
  KASSERT(east_cv != NULL);
  KASSERT(west_cv != NULL);
  KASSERT(car_list != NULL);

  lock_destroy(intersectionLock);
  cv_destroy(north_cv);
  cv_destroy(east_cv);
  cv_destroy(south_cv);
  cv_destroy(west_cv);
  array_destroy(car_list);
  // KASSERT(intersectionSem != NULL);
  // sem_destroy(intersectionSem);
}

bool canEnter(Car* incoming, Car* here) {
  if ((incoming->origin == here->origin) || 
  ((incoming->origin == here->destination) && (incoming->destination == here->origin)) ||
  ((incoming->destination != here->destination) && 
  (canRightTurn(incoming) || canRightTurn(here)))) {
    return true;
  }
  return false;
}

bool canRightTurn(Car* car) {
  if (((car->origin == north) && (car->destination == west)) ||
      ((car->origin == south) && (car->destination == east)) ||
      ((car->origin == east) && (car->destination == north)) ||
      ((car->origin == west) && (car->destination == south))) {
    return true;
  }
  return false;
}

bool 
safeToGo(Car * car) {
  for (unsigned int i = 0; i < array_num(car_list); ++i) {
    while (canEnter(car, array_get(car_list, i)) == false) {
      // kprintf("stuck?\n");
      if (car->destination == north) {
        cv_wait(north_cv, intersectionLock);
      } else if (car->destination== east) {
        cv_wait(east_cv, intersectionLock);
      } else if (car->destination == south) {
        cv_wait(south_cv, intersectionLock);
      } else {
        cv_wait(west_cv, intersectionLock);
      }
      return false;
    } // end canEnter if
  }
  // kprintf("safe");
  array_add(car_list, car, NULL);
  return true;
}

/*
 * The simulation driver will call this function each time a vehicle
 * tries to enter the intersection, before it enters.
 * This function should cause the calling simulation thread 
 * to block until it is OK for the vehicle to enter the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle is arriving
 *    * destination: the Direction in which the vehicle is trying to go
 *
 * return value: none
 */

void
intersection_before_entry(Direction origin, Direction destination) 
{
  KASSERT(intersectionLock != NULL);
  KASSERT(north_cv != NULL);
  KASSERT(south_cv != NULL);
  KASSERT(east_cv != NULL);
  KASSERT(west_cv != NULL);
  KASSERT(car_list != NULL);

  lock_acquire(intersectionLock);

  Car* car = kmalloc(sizeof(struct Car));
  car->origin = origin;
  car->destination = destination;

  while(safeToGo(car) == false) {
    // kprintf("hi");
  }

  lock_release(intersectionLock);
}


/*
 * The simulation driver will call this function each time a vehicle
 * leaves the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle arrived
 *    * destination: the Direction in which the vehicle is going
 *
 * return value: none
 */

void
intersection_after_exit(Direction origin, Direction destination) 
{
  // kprintf("exit\n");
  KASSERT(intersectionLock != NULL);
  KASSERT(north_cv != NULL);
  KASSERT(south_cv != NULL);
  KASSERT(east_cv != NULL);
  KASSERT(west_cv != NULL);
  KASSERT(car_list != NULL);

  lock_acquire(intersectionLock);

  for (unsigned int i = 0; i < array_num(car_list); ++i) {
    Car* thisCar = array_get(car_list, i);
    if ((origin == thisCar->origin) && (destination == thisCar->destination)) {
      array_remove(car_list, i);
      // notify everyone
      cv_broadcast(north_cv, intersectionLock);
      cv_broadcast(south_cv, intersectionLock);
      cv_broadcast(east_cv, intersectionLock);
      cv_broadcast(west_cv, intersectionLock);
      break;
    }
  }
  
  // kprintf("Number vehicles left: %d\n", array_num(car_list));

  lock_release(intersectionLock);
  // KASSERT(intersectionSem != NULL);
  // car(intersectionSem);
}
