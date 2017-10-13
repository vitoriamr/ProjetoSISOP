#include <stdio.h>
#include <stdlib.h>
#include "../include/support.h"
#include "../include/cdata.h"
#include <ucontext.h>
#define MEM 64000

typedef struct s_sem {
    int count;  // indica se recurso está ocupado ou não (livre > 0, ocupado = 0)
    PFILA2  fila;   // ponteiro para uma fila de threads bloqueadas no semáforo
} csem_t;

PFILA2 joinList;
PFILA2 executando, bloqueado;
// 0 = maior prioridade
PFILA2 aptos;
// T ID
int t_tid=0;

ucontext_t c_escalonador;

TCB_t *finder;

// inicializa as estruturas globais de apoio
void mainInicio(){ /** REFEITO **/
    executando = (malloc(sizeof(PFILA2)));
    bloqueado = (malloc(sizeof(PFILA2)));
    aptos = (malloc(sizeof(PFILA2)));
    joinList = (malloc(sizeof(PFILA2)));

    CreateFila2(executando);
    CreateFila2(bloqueado);
    CreateFila2(aptos);
    CreateFila2(joinList);

    finder = (TCB_t*)(malloc(sizeof(TCB_t)));

    //contexto escalonador
    getcontext(&c_escalonador);
    //c_escalonador.uc_link = &t_main->context;
    c_escalonador.uc_stack.ss_sp=malloc(MEM);
    c_escalonador.uc_stack.ss_size=MEM;
    makecontext(&c_escalonador, (void*)escalonador, 0);

    FirstFila2(executando);
    FirstFila2(bloqueado);
    FirstFila2(aptos);
    FirstFila2(joinList);
}

// recebe um tid e a thread que chamou cjoin fica esperando o fim da thread indicada por cjoin
int cjoin(int waitTid){ /** PENSADO **/
    /**
    passos:
        - mover a thread executando para bloqueado
        - salvar o waitTid numa lista (garantir que é unico)
            > waitTid-List : tid, tid de executando
        - chamar escalonador para selecionar proxima thread
    ESCALONADOR:
        - quando thread waitTid acabar, move  thread bloqueada para apto
    **/
}
// libera voluntariamente o CPU e chama a proxima thread para executar
int cyield(){ /** PENSADO **/

    /**
    - mover de executando para apto
    - chamar escalonador para selecionar proxima thread
    **/
}
// inicializa todas as estruturas para uma thread e a insere na fila apropriada a sua prioridade
int ccreate (void (start)(void), void *arg, int prio){ /* REFEITO **/
    int r;
    //inicializa estruturas
    if(t_tid == 0){
        mainInicio();
    }
    t_tid++;
    //inicializa TCB
    TCB_t t_helper = (TCB_t)(malloc(sizeof(TCB_t)));

    //cria contexto
    getcontext(&t_helper->context);
    t_helper->context.uc_link = &c_escalonador;
    t_helper->context.uc_stack.ss_sp=malloc(MEM);
    t_helper->context.uc_stack.ss_size=MEM;
    makecontext(&t_helper->context, start, 1, arg);

    //seta prioridade
    t_helper->ticket = prio;

    //seta tid
    t_helper->tid = t_tid;

    //estado
    t_helper->state = 0;

    InsertByPrio(aptos, t_helper)

    return r == 0 ? t_helper->tid : -1;
}
