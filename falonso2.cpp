/*
 * PROGRAMA FALONSO2.C
 * Autores: Carlos Manjón García y Miguel Sánchez González  22-MAY-2019.
 */
#include <iostream>
#include <stdio.h>
#include <Windows.h>
#include <signal.h>
#include <time.h>
#include "falonso2.h"


/* Constantes de la funcion exit().  */
#define ERR_ARGS (unsigned char)151  /* Error de argumentos.  */
#define ERR_PIST (unsigned char)152  /* Error al crear evento pistoletazo.  */
#define ERR_HILO (unsigned char)153  /* Error al crear nuevo hilo.  */
#define ERR_DLL (unsigned char)154  /* Error al cargar biblioteca DLL.  */
#define ERR_SEM (unsigned char)155  /* Error al crear un semáforo.  */
#define ERR_MEM (unsigned char)156  /* Error en la memoria compartida.  */

/* Constantes de los semáforos.  */
#define SEM_ATOM 274  /* Semáforo para partes atómicas.  */

/* Número máximo de hijos.  */
#define NUM_HIJOS 25
#define NUM_SEM 275
#define T_EJECUCION 30000


/* Cargamos la libreria DLL.  */
HINSTANCE libreria = LoadLibrary("falonso2.dll");


/* Funciones de los hijos.  */
DWORD WINAPI funcionHijo(LPVOID parametro);
int puedoAvanzar(int carril, int desp);
int puedoCambiarCarril(int carril, int desp);

/* Funciones de control.  */
DWORD WINAPI controlCruce(LPVOID parametro);
void abrirCruce(int dirCruce);
void cerrarCruce(int dirCruce);

/* Definiciones de las funciones de librería.  */
int (*FALONSO2_inicio)(int);
int (*FALONSO2_fin)(int*);
int (*FALONSO2_luz_semAforo)(int, int);
int (*FALONSO2_estado_semAforo)(int);
int (*FALONSO2_inicio_coche)(int*, int*, int);
int (*FALONSO2_avance_coche)(int*, int*, int);
int (*FALONSO2_velocidad)(int, int, int);
int (*FALONSO2_cambio_carril)(int*, int*, int);
int (*FALONSO2_posiciOn_ocupada)(int, int);
int (*FALONSO2_pausa)(void);
void (*pon_error)(const char*);


/* Variables globales.  */
HANDLE sem[NUM_SEM];
int numVueltas = 0;
HANDLE pistoletazo;



int main(int argc, const char *argv[]) {

	/* Comprobamos que se han introducido correctamente los argumentos.  */
	if (argc != 3) {
		puts("Modo de uso, \"./falonso (numero de coches) (velocidad 0/1)\".");
		exit(ERR_ARGS);
	}

	const int num_coches = atoi(argv[1]);  /* Número de hijos.  */
	const int vel_coches = atoi(argv[2]);  /* Velocidad de los coches.  */
	if ((num_coches <= 0) && (num_coches > NUM_HIJOS)) {
		printf("Número de coches erróneo (max. %d).", NUM_HIJOS);
		exit(ERR_ARGS);
	}
	else if ((vel_coches != 0) && (vel_coches != 1)) {
		puts("Velocidad de coches 1(normal) o 0(rápida).");
		exit(ERR_ARGS);
	}


	/* Comprobamos que la librería se ha cargado correctamente.  */
	if (libreria == NULL) {
		perror("Error al cargar DLL");
		exit(ERR_DLL);
	}

	/* Incluimos las funciones de la dll.  */
	FALONSO2_inicio = (int(*)(int))GetProcAddress(libreria, "FALONSO2_inicio");
	FALONSO2_fin = (int(*)(int*))GetProcAddress(libreria, "FALONSO2_fin");
	FALONSO2_luz_semAforo = (int(*)(int, int))GetProcAddress(libreria, "FALONSO2_luz_semAforo");
	FALONSO2_estado_semAforo = (int(*)(int))GetProcAddress(libreria, "FALONSO2_estado_semAforo");
	FALONSO2_inicio_coche = (int(*)(int*, int*, int))GetProcAddress(libreria, "FALONSO2_inicio_coche");
	FALONSO2_avance_coche = (int(*)(int*, int*, int))GetProcAddress(libreria, "FALONSO2_avance_coche");
	FALONSO2_velocidad = (int(*)(int, int, int))GetProcAddress(libreria, "FALONSO2_velocidad");
	FALONSO2_cambio_carril = (int(*)(int*, int*, int))GetProcAddress(libreria, "FALONSO2_cambio_carril");
	FALONSO2_posiciOn_ocupada = (int(*)(int, int))GetProcAddress(libreria, "FALONSO2_posiciOn_ocupada");
	pon_error = (void(*)(const char*))GetProcAddress(libreria, "pon_error");

	/* Memoria Compartida.  */
	int flagHijos = 1;
	int flagCruce = 1;
	HANDLE memoriaHijos;
	HANDLE memoriaCruce;
	LPVOID refHijos;
	LPVOID refCruce;

	/* Creamos la memoria compartida para los hijos, la asignamos a un puntero y le damos un valor.  */
	if (NULL == (memoriaHijos = CreateFileMapping(NULL, NULL, PAGE_READWRITE, 0, sizeof(int), "salir"))) {
		perror("Error al crear memoria");
		exit(ERR_MEM);
	}
	if (NULL == (refHijos = (LPVOID)MapViewOfFile(memoriaHijos, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(int)))) {
		perror("Error al mapear memoria");
		exit(ERR_MEM);
	}
	CopyMemory(refHijos, &flagHijos, sizeof(int));

	/* Creamos la memoria compartida para el padre, la asignamos a un puntero y le damos un valor.  */
	if (NULL == (memoriaCruce = CreateFileMapping(NULL, NULL, PAGE_READWRITE, 0, sizeof(int), "salirC"))) {
		perror("Error al crear memoria");
		exit(ERR_MEM);
	}
	if (NULL == (refCruce = (LPVOID)MapViewOfFile(memoriaCruce, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(int)))) {
		perror("Error al mapear memoria");
		exit(ERR_MEM);
	}
	CopyMemory(refCruce, &flagCruce, sizeof(int));


	/* Declaración de Variables */
	HANDLE cruce;  /* Hilo del padre.  */
	HANDLE hijos[NUM_HIJOS];  /* Array de hilos de los hijos.  */


	/* Asignamos pistoletazo a un evento.  */
	pistoletazo = CreateEvent(NULL, TRUE, FALSE, "pistoletazo");
	if (pistoletazo == NULL) {
		perror("Error al crear evento pistoletazo");
		exit(ERR_PIST);
	}


	/* Declaramos los semáforos.  */
	for (int i = 0; i < NUM_SEM; i++) {
		sem[i] = CreateSemaphore(NULL, 1, 1, NULL);
		if (sem[i] == NULL) {
			perror("Error al crear semaforo");
			exit(ERR_SEM);
		}
	}

	/* Iniciamos el circuito.  */
	FALONSO2_inicio(vel_coches);


	/* Creamos un hilo por cada hijo.  */
	for (int i = 0; i < num_coches; i++) {
		hijos[i] = CreateThread(NULL, 0, funcionHijo, LPVOID(i), 0, NULL);
		if (hijos[i] == NULL) {
			perror("Error al crear el hilo");
			exit(ERR_HILO);
		}
	}

	/* Creamos Proceso que controla el cruce */
	cruce = CreateThread(NULL, 0, controlCruce, 0, 0, NULL);
	if (cruce == NULL) {
		perror("Error al crear el hilo");
		exit(ERR_HILO);
	}

	/* Damos el pistoletazo de salida.  */
	SetEvent(pistoletazo);


	/* Ejecutamos el programam durante este tiempo.  */
	Sleep(T_EJECUCION);

	flagCruce = 0;
	CopyMemory(refCruce, &flagCruce, sizeof(int));
	WaitForSingleObject(pistoletazo, INFINITE);

	flagHijos = 0;
	CopyMemory(refHijos, &flagHijos, sizeof(int));
	/* Cierre del Programa.  */
	FALONSO2_fin(&numVueltas);

	/* Finalizamos todos los hilos de los hijos.  */
	for (int i = 0; i < num_coches; i++)
		CloseHandle(hijos[i]);

	/* Eliminamos todos los semáforos.  */
	for (int i = 0; i < NUM_SEM; i++)
		CloseHandle(sem[i]);

	/* Eliminamos el hilo del padre.  */
	CloseHandle(cruce);

	/* Liberamos el evento.  */
	CloseHandle(pistoletazo);

	/* Cerrramos Memoria Comparida */
	UnmapViewOfFile(refHijos);
	UnmapViewOfFile(refCruce);
	CloseHandle(memoriaHijos);
	CloseHandle(memoriaCruce);

	/* Descargamos la librería.  */
	FreeLibrary(libreria);

	return 0;

}


/*
 * Función para los hilos de los hijos(coches).
 */
DWORD WINAPI funcionHijo(LPVOID parametro)
{
	HANDLE memoria = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, "salir");
	LPVOID ref = (LPVOID)MapViewOfFile(memoria, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(int));
	int* flagHijos = (int*)ref;

	/* Calculamos la semilla para la función rand().  */
	srand((int)parametro);

	/* Variables del hijo.  */
	int carril = (int)parametro % 2;  /* Carril del coche.  */
	int desp = (int)parametro / 2;  /* Posición del coche.  */
	int color = rand() % 8;  /* Color del coche.  */
	int velo = 1 + (rand() % 99);  /* Velocidad del coche.  */
	int tempC, tempD;  /* Variables temporales para el cambio de posición.  */
	int cambio = 0;  /* Flag para indicar si se ha cambiado de carril.  */

	/* Si calcula un color azul lo cambia a otro.  */
	if (1 == color)  color++;

	/* Reservamos la posición de salida del coche.  */
	if (carril == CARRIL_DERECHO)
		WaitForSingleObject(sem[desp], INFINITE);
	else
		WaitForSingleObject(sem[137 + desp], INFINITE);

	/* Ponemos el coche en la posición de salida.  */
	FALONSO2_inicio_coche(&carril, &desp, color);


	/* Esperar todos los procesos, evento pistoletazo.  */
	WaitForSingleObject(pistoletazo, INFINITE);


	/* Bucle de ejecución.  */
	while (*flagHijos) {
		ref = (LPVOID)MapViewOfFile(memoria, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(int));
		int* flagHijos = (int*)ref;

		FALONSO2_velocidad(velo, carril, desp);

		if (puedoAvanzar(carril, desp)) {
			/* Inicio parte atómica.  */
			WaitForSingleObject(sem[SEM_ATOM], INFINITE);
			/*----------------------------------------------------------------*/
			if (carril == CARRIL_DERECHO)
				ReleaseSemaphore(sem[desp], 1, NULL);
			else
				ReleaseSemaphore(sem[desp + 137], 1, NULL);

			FALONSO2_avance_coche(&carril, &desp, color);

			if (((carril == 0) && (desp == 133)) || ((carril == 1) && (desp == 131)))
				numVueltas++;
			/*----------------------------------------------------------------*/
			ReleaseSemaphore(sem[SEM_ATOM], 1, NULL);
			/* Fin parte atómica.  */

			cambio = 0;

			continue;
		}
		else if ((cambio < 2) && puedoCambiarCarril(carril, desp)) {
			/* Inicio parte atómica.  */
			WaitForSingleObject(sem[SEM_ATOM], INFINITE);
			/*----------------------------------------------------------------*/
			tempC = carril;
			tempD = desp;
			FALONSO2_cambio_carril(&carril, &desp, color);

			if (tempC == 0)
				ReleaseSemaphore(sem[tempD], 1, NULL);
			else
				ReleaseSemaphore(sem[tempD + 137], 1, NULL);
			/*----------------------------------------------------------------*/
			ReleaseSemaphore(sem[SEM_ATOM], 1, NULL);
			/* Fin parte atómica.  */

			cambio++;

			continue;
		}


		if (carril == CARRIL_DERECHO) {
			if (desp == 136)
				WaitForSingleObject(sem[0], INFINITE);
			else
				WaitForSingleObject(sem[desp + 1], INFINITE);
		}
		else {
			if (desp == 136)
				WaitForSingleObject(sem[137], INFINITE);
			else
				WaitForSingleObject(sem[137 + desp + 1], INFINITE);
		}

		/* Inicio parte atómica.  */
		WaitForSingleObject(sem[SEM_ATOM], INFINITE);
		/*--------------------------------------------------------------------*/
		if (carril == CARRIL_DERECHO)
			ReleaseSemaphore(sem[desp], 1, NULL);
		else
			ReleaseSemaphore(sem[desp + 137], 1, NULL);

		FALONSO2_avance_coche(&carril, &desp, color);

		if (((carril == 0) && (desp == 133)) || ((carril == 1) && (desp == 131)))
			numVueltas++;
		/*--------------------------------------------------------------------*/
		ReleaseSemaphore(sem[SEM_ATOM], 1, NULL);
		/* Fin parte atómica.  */

		cambio = 0;
	}

	/* Cerrar Memoria Compartida */
	UnmapViewOfFile(ref);
	CloseHandle(memoria);

	/* Esperar Fin Programa */
	WaitForSingleObject(pistoletazo, INFINITE);

	return 1;
}


/*
 * Función para el hilo de control del cruce.
 */
DWORD WINAPI controlCruce(LPVOID parametro)
{
	HANDLE memoria = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, "salirC");
	LPVOID ref = (LPVOID)MapViewOfFile(memoria, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(int));
	int* flagCruce = (int*)ref;

	/* Control del padre.  */
	cerrarCruce(HORIZONTAL);

	if (-1 == FALONSO2_luz_semAforo(HORIZONTAL, ROJO))
		pon_error("Error luz semaforo");

	if (-1 == FALONSO2_luz_semAforo(VERTICAL, VERDE))
		pon_error("Error luz semaforo");


	/* Sincronizacion de los semáforos y cambio de color de estos.  */
	while (*flagCruce) {
		ref = (LPVOID)MapViewOfFile(memoria, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(int));
		int* flagCruce = (int*)ref;

		/* Control semáforo (V=0  H=1).  */
		if (-1 == FALONSO2_luz_semAforo(VERTICAL, AMARILLO))
			pon_error("Error luz semaforo");

		cerrarCruce(VERTICAL);

		if (-1 == FALONSO2_luz_semAforo(VERTICAL, ROJO))
			pon_error("Error luz semaforo");

		if (-1 == FALONSO2_luz_semAforo(HORIZONTAL, VERDE))
			pon_error("Error luz semaforo");

		abrirCruce(HORIZONTAL);

		Sleep(1000);

		/* Control semáforo (V=1  H=0).  */
		if (-1 == FALONSO2_luz_semAforo(HORIZONTAL, AMARILLO))
			pon_error("Error luz semaforo");

		cerrarCruce(HORIZONTAL);

		if (-1 == FALONSO2_luz_semAforo(HORIZONTAL, ROJO))
			pon_error("Error luz semaforo");

		if (-1 == FALONSO2_luz_semAforo(VERTICAL, VERDE))
			pon_error("Error luz semaforo");

		abrirCruce(VERTICAL);

		Sleep(1000);
	}

	/* Avisar Padre Salida de Bucle */
	SetEvent(pistoletazo);

	/* Cerrar Memoria Compartida */
	UnmapViewOfFile(ref);
	CloseHandle(memoria);

	/* Esperar Fin Programa */
	WaitForSingleObject(pistoletazo, INFINITE);

	return 1;
}


/*
 * Función para comprobar si un coche puede avanzar de posición.
 */
int puedoAvanzar(int carril, int desp)
{
	if (carril == CARRIL_DERECHO) {
		if (desp == 136) {
			if (FALONSO2_posiciOn_ocupada(carril, 0))
				return 0;
			else {
				WaitForSingleObject(sem[0], INFINITE);
				return 1;
			}
		}
		else {
			if (FALONSO2_posiciOn_ocupada(carril, desp + 1))
				return 0;
			else {
				WaitForSingleObject(sem[desp + 1], INFINITE);
				return 1;
			}
		}
	}
	else {
		if (desp == 136) {
			if (FALONSO2_posiciOn_ocupada(carril, 0))
				return 0;
			else {
				WaitForSingleObject(sem[137], INFINITE);
				return 1;
			}
		}
		else {
			if (FALONSO2_posiciOn_ocupada(carril, desp + 1))
				return 0;
			else {
				WaitForSingleObject(sem[desp + 1 + 137], INFINITE);
				return 1;
			}
		}
	}
}


/*
 * Función para comprobar si un coche puede cambiar de carril.
 */
int puedoCambiarCarril(int carril, int desp)
{
	if (carril == CARRIL_DERECHO) {
		if (desp >= 14 && desp <= 28) {
			if (desp >= 21 && desp <= 24) {
				if (!FALONSO2_posiciOn_ocupada(carril + 1, desp + 1)
					&& (FALONSO2_estado_semAforo(HORIZONTAL) == VERDE)) {
					WaitForSingleObject(sem[137 + desp + 1], INFINITE);
					return 1;
				}
				else
					return 0;
			}
			else if (!FALONSO2_posiciOn_ocupada(carril + 1, desp + 1)) {
				WaitForSingleObject(sem[137 + desp + 1], INFINITE);
				return 1;
			}
			else
				return 0;
		}
		else if ((desp >= 0 && desp <= 13) || (desp >= 29 && desp <= 60)) {
			if (!FALONSO2_posiciOn_ocupada(carril + 1, desp)) {
				WaitForSingleObject(sem[137 + desp], INFINITE);
				return 1;
			}
			else
				return 0;
		}
		else if ((desp >= 61 && desp <= 62) || (desp >= 135 && desp <= 136)) {
			if (!FALONSO2_posiciOn_ocupada(carril + 1, desp - 1)) {
				WaitForSingleObject(sem[137 + desp - 1], INFINITE);
				return 1;
			}
			else
				return 0;
		}
		else if ((desp >= 63 && desp <= 65) || (desp >= 131 && desp <= 134)) {
			if (!FALONSO2_posiciOn_ocupada(carril + 1, desp - 2)) {
				WaitForSingleObject(sem[137 + desp - 2], INFINITE);
				return 1;
			}
			else
				return 0;
		}
		else if ((desp >= 66 && desp <= 67) || (desp == 130)) {
			if (!FALONSO2_posiciOn_ocupada(carril + 1, desp - 3)) {
				WaitForSingleObject(sem[137 + desp - 3], INFINITE);
				return 1;
			}
			else
				return 0;
		}
		else if (desp == 68) {
			if (!FALONSO2_posiciOn_ocupada(carril + 1, desp - 4)) {
				WaitForSingleObject(sem[137 + desp - 4], INFINITE);
				return 1;
			}
			else
				return 0;
		}
		else {
			if (!FALONSO2_posiciOn_ocupada(carril + 1, desp - 5)) {
				WaitForSingleObject(sem[137 + desp - 5], INFINITE);
				return 1;
			}
			else
				return 0;
		}
	}
	else {
		if (desp >= 16 && desp <= 28) {
			if (desp >= 23 && desp <= 26) {
				if (!FALONSO2_posiciOn_ocupada(carril - 1, desp - 1)
					&& (FALONSO2_estado_semAforo(HORIZONTAL) == VERDE)) {
					WaitForSingleObject(sem[desp - 1], INFINITE);
					return 1;
				}
				else
					return 0;
			}
			else if (!FALONSO2_posiciOn_ocupada(carril - 1, desp - 1)) {
				WaitForSingleObject(sem[desp - 1], INFINITE);
				return 1;
			}
			else
				return 0;
		}
		else if ((desp >= 0 && desp <= 15) || (desp >= 29 && desp <= 58)) {
			if (!FALONSO2_posiciOn_ocupada(carril - 1, desp)) {
				WaitForSingleObject(sem[desp], INFINITE);
				return 1;
			}
			else
				return 0;
		}
		else if (desp >= 59 && desp <= 60) {
			if (!FALONSO2_posiciOn_ocupada(carril - 1, desp + 1)) {
				WaitForSingleObject(sem[desp + 1], INFINITE);
				return 1;
			}
			else
				return 0;
		}
		else if ((desp >= 61 && desp <= 62) || (desp >= 129 && desp <= 133)) {
			if (!FALONSO2_posiciOn_ocupada(carril - 1, desp + 2)) {
				WaitForSingleObject(sem[desp + 2], INFINITE);
				return 1;
			}
			else
				return 0;
		}
		else if (desp >= 127 && desp <= 128) {
			if (!FALONSO2_posiciOn_ocupada(carril - 1, desp + 3)) {
				WaitForSingleObject(sem[desp + 3], INFINITE);
				return 1;
			}
			else
				return 0;
		}
		else if ((desp >= 63 && desp <= 64) || (desp == 126)) {
			if (!FALONSO2_posiciOn_ocupada(carril - 1, desp + 4)) {
				WaitForSingleObject(sem[desp + 4], INFINITE);
				return 1;
			}
			else
				return 0;
		}
		else if (desp >= 65 && desp <= 125) {
			if (desp >= 99 && desp <= 103) {
				if (!FALONSO2_posiciOn_ocupada(carril - 1, desp + 5)
					&& (FALONSO2_estado_semAforo(VERTICAL) == VERDE)) {
					WaitForSingleObject(sem[desp + 5], INFINITE);
					return 1;
				}
				else
					return 0;
			}
			if (!FALONSO2_posiciOn_ocupada(carril - 1, desp + 5)) {
				WaitForSingleObject(sem[desp + 5], INFINITE);
				return 1;
			}
			else
				return 0;
		}
		else {
			if (!FALONSO2_posiciOn_ocupada(carril - 1, 136)) {
				WaitForSingleObject(sem[136], INFINITE);
				return 1;
			}
			else
				return 0;
		}
	}
}


/*
 * Libera los semáforos de las posiciones del cruce para permitir el paso.
 */
void abrirCruce(int dirCruce)
{
	if (dirCruce == VERTICAL) {
		ReleaseSemaphore(sem[21], 1, NULL);
		ReleaseSemaphore(sem[137 + 23], 1, NULL);
		ReleaseSemaphore(sem[22], 1, NULL);
		ReleaseSemaphore(sem[137 + 24], 1, NULL);
		ReleaseSemaphore(sem[23], 1, NULL);
		ReleaseSemaphore(sem[137 + 25], 1, NULL);
	}
	else {
		ReleaseSemaphore(sem[106], 1, NULL);
		ReleaseSemaphore(sem[137 + 99], 1, NULL);
		ReleaseSemaphore(sem[107], 1, NULL);
		ReleaseSemaphore(sem[137 + 100], 1, NULL);
		ReleaseSemaphore(sem[108], 1, NULL);
		ReleaseSemaphore(sem[137 + 101], 1, NULL);
	}
}


/*
 * Bloquea los semáforos de las posiciones del cruce para prohibir el paso.
 */
void cerrarCruce(int dirCruce)
{
	if (dirCruce == VERTICAL) {
		WaitForSingleObject(sem[21], INFINITE);
		WaitForSingleObject(sem[137 + 23], INFINITE);
		WaitForSingleObject(sem[22], INFINITE);
		WaitForSingleObject(sem[137 + 24], INFINITE);
		WaitForSingleObject(sem[23], INFINITE);
		WaitForSingleObject(sem[137 + 25], INFINITE);
	}
	else {
		WaitForSingleObject(sem[106], INFINITE);
		WaitForSingleObject(sem[137 + 99], INFINITE);
		WaitForSingleObject(sem[107], INFINITE);
		WaitForSingleObject(sem[137 + 100], INFINITE);
		WaitForSingleObject(sem[108], INFINITE);
		WaitForSingleObject(sem[137 + 101], INFINITE);
	}
}
