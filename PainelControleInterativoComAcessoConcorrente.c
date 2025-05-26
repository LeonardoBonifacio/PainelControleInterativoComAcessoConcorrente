#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "hardware/gpio.h"  // Funções padrões para manusear as Gpios
#include "hardware/i2c.h"   // Para realizar a conexão e comunicação com o display oled 
#include "hardware/pwm.h"   // Para controlar o buzzer da gpio 21
#include "hardware/pio.h"   // Para controlar a matriz de leds da gpio 7
#include "lib/ssd1306.h"    // Para desenhar no diplay oled
#include "lib/font.h"       // Fonte usada no display oled
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"
#include "ctype.h"
#include "stdbool.h"

// Biblioteca gerada pelo arquivo .pio durante compilação.
#include "ws2812.pio.h"

// Variavéis para botões
#define DEBOUNCE_US_BUTTONS 200000

#define BOTAO_JOYSTICK 22
static volatile uint32_t last_time_JOY = 0; 

#define BOTAO_A 5
static volatile uint32_t last_time_A = 0; 

#define BOTAO_B 6
static volatile uint32_t last_time_B = 0; 

// Variavéis do leds
#define LED_AZUL 12
#define LED_VERMELHO 13
#define LED_VERDE 11
#define GPIO_MATRIZ_LEDS 7
#define QUANTIDADE_LEDS_MATRIZ 25

// Variáveis para uso do buzzer
#define GPIO_PIN_BUZZER 21
#define WRAP  12500 

// Para ficar mais facil de entender a lógica de algumas funções 
#define MAXIMO_USUARIOS 5
#define MINIMO_USUARIOS 0

// Para conexão i2c e configuração do display oled
#define I2C_PORT i2c1
#define I2C_SDA 14
#define I2C_SCL 15
#define endereco 0x3C // Endereço fisico display ssd1306
ssd1306_t ssd;

// Mutex usado para compartilhar o display de forma segura
SemaphoreHandle_t xMutexDisplay;
//Semáforo para contar a quantidade maxima de usuários no sistema e a quantidade atual
SemaphoreHandle_t xSemaforoContagemUsuários;
// Semáforos binários usados para sinalização
SemaphoreHandle_t xSemaforoBinarioResetSistema;// Sinaliza quando o botão do joystick foi apertado e libera a task de reset para fazer seu trabalho
SemaphoreHandle_t xSemaforoBinarioAumentarUsuarios;// Para quando o botão A for apertado 
SemaphoreHandle_t xSemaforoBinarioProsseguirComLoginOuLogout; // Apos o botão A ou B for apertado na tela de opções de login ou logout
SemaphoreHandle_t xSemaforoBinarioDiminuirUsuarios; // Para quando  o botão B for apertado
SemaphoreHandle_t xSemaforoBinarioAvisarLimiteUsuarios; // Para ativar a task de limite de usuários(5)
SemaphoreHandle_t xSemaforoBinarioIDInformado;// Para quando um ID for informado pelo teclado

QueueHandle_t xFilaCodigoIdentificacaoUsuario;// Fila com 1 espaço , para compartilhar o id capturado pelo teclado entre as tasks

// struct que define o usuário
typedef struct {
    char codigo;
    char nome[10];
    bool logado;
} PerfilUsuario;

// Enum para evitar flicks na tela ao trocar de estado no sistema(ao ficar na tela inicial para a tela de espera do botão)
typedef enum {
    ESPERANDO_ID,
    ESPERANDO_BOTÃO
}Display_Modo;

// Esta inicial do display
Display_Modo modo_display = ESPERANDO_ID;

// Usuários cadastrados para demonstração
PerfilUsuario usuarios[] = {
    {'A', "ALICE",false},
    {'B', "BRUNO",false},
    {'C', "CARLA",false},
    {'D', "DIEGO",false},
    {'E', "ELAINE",false},
};

// Buffer para armazenar quais LEDs estão ligados matriz 5x5 formando numero 0
bool sem_usuarios[QUANTIDADE_LEDS_MATRIZ] = {
    0, 1, 1, 1, 1, 
    1, 0, 0, 1, 0, 
    0, 1, 0, 0, 1, 
    1, 0, 0, 1, 0, 
    0, 1, 1, 1, 1
};


// Buffer para armazenar quais LEDs estão ligados matriz 5x5 formando numero 1
bool um_usuario[QUANTIDADE_LEDS_MATRIZ] = {
    0, 0, 1, 0, 0, 
    0, 0, 1, 0, 0, 
    0, 0, 1, 0, 1, 
    0, 1, 1, 0, 0, 
    0, 0, 1, 0, 0
};

// Buffer para armazenar quais LEDs estão ligados matriz 5x5 formando numero 2
bool dois_usuarios[QUANTIDADE_LEDS_MATRIZ] = {
    0, 1, 1, 1, 1, 
    1, 0, 0, 0, 0, 
    0, 1, 1, 1, 1, 
    0, 0, 0, 1, 0, 
    0, 1, 1, 1, 1
};

// Buffer para armazenar quais LEDs estão ligados matriz 5x5 formando numero 3
bool tres_usuarios[QUANTIDADE_LEDS_MATRIZ] = {
    0, 1, 1, 1, 1, 
    0, 0, 0, 1, 0, 
    0, 1, 1, 1, 1, 
    0, 0, 0, 1, 0, 
    0, 1, 1, 1, 1
};


// Buffer para armazenar quais LEDs estão ligados matriz 5x5 formando numero 4
bool quatro_usuarios[QUANTIDADE_LEDS_MATRIZ] = {
    0, 1, 0, 0, 0, 
    0, 0, 0, 1, 0, 
    0, 1, 1, 1, 1, 
    1, 0, 0, 1, 0, 
    0, 1, 0, 0, 1
};


// Buffer para armazenar quais LEDs estão ligados matriz 5x5 formando numero 5
bool cinco_usuarios[QUANTIDADE_LEDS_MATRIZ] = {
    0, 1, 1, 1, 1, 
    0, 0, 0, 1, 0, 
    0, 1, 1, 1, 1, 
    1, 0, 0, 0, 0, 
    0, 1, 1, 1, 1
};

//Funções ======================================================================


// ISR para gpios dos botões
void gpio_irq_handler(uint gpio, uint32_t events) {
    // para verificar se apos sair da interrupção uma task de prioridade maior que estava esperando o recurso liberado pelo semaforo foi "acordada"
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    uint32_t current_time = to_us_since_boot(get_absolute_time()); // Para debounce dos botões

    if (gpio == BOTAO_A) {
        if (current_time - last_time_A > DEBOUNCE_US_BUTTONS) {  // 200ms debounce
            last_time_A = current_time;
            // Verifica se os semáforos fizeram com que o sistema necessitase de uma troca de contexto
            xSemaphoreGiveFromISR(xSemaforoBinarioAumentarUsuarios, &xHigherPriorityTaskWoken);
            xSemaphoreGiveFromISR(xSemaforoBinarioProsseguirComLoginOuLogout, &xHigherPriorityTaskWoken);
            // Caso sim, a task de maior prioridade que "acordou" apos o semáforo liberar o recurso será a primeira a ser executada
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken); 
        }
    }
    else if (gpio == BOTAO_B) {
        if (current_time - last_time_B > DEBOUNCE_US_BUTTONS) {  // 200ms debounce
            last_time_B = current_time;
            xSemaphoreGiveFromISR(xSemaforoBinarioDiminuirUsuarios, &xHigherPriorityTaskWoken);
            xSemaphoreGiveFromISR(xSemaforoBinarioProsseguirComLoginOuLogout, &xHigherPriorityTaskWoken);
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        }
    }
    else if (gpio == BOTAO_JOYSTICK) {
        if (current_time - last_time_JOY > DEBOUNCE_US_BUTTONS) {  // 200ms debounce
            last_time_JOY = current_time;
            xSemaphoreGiveFromISR(xSemaforoBinarioResetSistema, &xHigherPriorityTaskWoken);
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        }
    }
}


// Para mandar um valor grb de 32bits(mas so 24 sendo usados) para a maquina de estado 0 do bloco 0 do PIO
static inline void put_pixel(uint32_t pixel_grb){
    pio_sm_put_blocking(pio0, 0, pixel_grb << 8u);
}

// cria um valor grb de 32 bits
static inline uint32_t urgb_u32(uint8_t r, uint8_t g, uint8_t b){
    return ((uint32_t)(r) << 8) | ((uint32_t)(g) << 16) | (uint32_t)(b);
}

void set_one_led(uint8_t r, uint8_t g, uint8_t b, bool desenho[]){
    // Define a cor com base nos parâmetros fornecidos
    uint32_t color = urgb_u32(r, g, b);

    // Define todos os LEDs com a cor especificada
    for (int i = 0; i < QUANTIDADE_LEDS_MATRIZ; i++)
    {
        if (desenho[i])
        {
            put_pixel(color); // Liga o LED com um no buffer
        }
        else
        {
            put_pixel(0);  // Desliga os LEDs com zero no buffer
        }
    }
}





//TASKS ======================================================================


// Verifica se um caractere foi digitado pelo teclado , se sim o envia para a fila sobrescrevendo o caractere anterior e enviar uma sinalização a um semáforo
void vTaskUART(void *params) {
    char c;
    while(true) {
        int rc = getchar_timeout_us(0);
        if (rc != PICO_ERROR_TIMEOUT) {
            c = (char)rc;
            xQueueOverwrite(xFilaCodigoIdentificacaoUsuario, &c);
            xSemaphoreGive(xSemaforoBinarioIDInformado);
            modo_display = ESPERANDO_BOTÃO;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}


 // Task que controla a matriz de leds 
void vMatrizLedTask(void * params){
    // Inicializa o bloco pio,sua state machine e o programa em assembly
    PIO pio = pio0;
    int sm = 0;
    uint offset = pio_add_program(pio, &ws2812_program);
    ws2812_program_init(pio, sm, offset, GPIO_MATRIZ_LEDS, 800000, false);
    // Verifica a quantidade de usuários ativos e mostra o número correspondente na matriz de leds
    // respeitando a cor exibida pelo led rgb que indica a ocupação atual do sistema
    while (true){
        if (uxSemaphoreGetCount(xSemaforoContagemUsuários) == MINIMO_USUARIOS){
            set_one_led(0,0,10,sem_usuarios);
        }else if (uxSemaphoreGetCount(xSemaforoContagemUsuários) == MAXIMO_USUARIOS - 4){ // Um Usuario logado
            set_one_led(0,10,0,um_usuario);
        }else if (uxSemaphoreGetCount(xSemaforoContagemUsuários) == MAXIMO_USUARIOS - 3){// Usuários ativos de 0 a MAX-2    MAX = 5
            set_one_led(0,10,0,dois_usuarios);
        }else if (uxSemaphoreGetCount(xSemaforoContagemUsuários) == MAXIMO_USUARIOS - 2){ // Usuários ativos de 0 a MAX-2    MAX = 5
            set_one_led(0,10,0,tres_usuarios);
        }else if (uxSemaphoreGetCount(xSemaforoContagemUsuários) == MAXIMO_USUARIOS - 1){ // Apenas 1 vaga restante (MAX - 1)
            set_one_led(10,10,0,quatro_usuarios);
        }else if(uxSemaphoreGetCount(xSemaforoContagemUsuários) == MAXIMO_USUARIOS){ // Capaxidade máxima
            set_one_led(10,0,0,cinco_usuarios);
        }else{
            set_one_led(0,0,0,cinco_usuarios);
        }
        vTaskDelay(pdMS_TO_TICKS(150));
    }

}


// Liga as cores relativas ao estado atual do sistema no led rgb
void vTaskEstadoSistemaComRgb(void *params){
    gpio_init(LED_AZUL);
    gpio_init(LED_VERDE);
    gpio_init(LED_VERMELHO);
    gpio_set_dir(LED_AZUL,GPIO_OUT);
    gpio_set_dir(LED_VERDE,GPIO_OUT);
    gpio_set_dir(LED_VERMELHO,GPIO_OUT);

    while (true){
        if (uxSemaphoreGetCount(xSemaforoContagemUsuários) == 0){ // Nenhum usuário logado
            gpio_put(LED_VERDE,0);
            gpio_put(LED_VERMELHO,0);
            gpio_put(LED_AZUL,1);
        }else if (uxSemaphoreGetCount(xSemaforoContagemUsuários) <= MAXIMO_USUARIOS - 2){// Usuários ativos de 0 a MAX-2    MAX = 5
            gpio_put(LED_VERMELHO,0);
            gpio_put(LED_AZUL,0);
            gpio_put(LED_VERDE,1);
        }else if (uxSemaphoreGetCount(xSemaforoContagemUsuários) == MAXIMO_USUARIOS - 1){ // Apenas 1 vaga restante (MAX - 1)
            gpio_put(LED_AZUL,0);
            gpio_put(LED_VERMELHO,1);
            gpio_put(LED_VERDE,1);
        }else if(uxSemaphoreGetCount(xSemaforoContagemUsuários) == MAXIMO_USUARIOS){ // Capaxidade máxima
            gpio_put(LED_AZUL,0);
            gpio_put(LED_VERDE,0);
            gpio_put(LED_VERMELHO,1);
        }
        vTaskDelay(pdMS_TO_TICKS(150));
    }
    
}

// Task que exibe a mensagem inicial padrão de espera de ID pelo teclado
void vTaskMostraMensagemDeEsperaPorID(void *params){
    while (true){
        if (modo_display == ESPERANDO_ID){ // Se o modo do display for este
            if (xSemaphoreTake(xMutexDisplay,portMAX_DELAY) == pdTRUE){ // Tenta capturar o mutex para utilização segura do display
                // Exibe a mensagem
                ssd1306_rect(&ssd, 1,1,122,60, true, false);
                ssd1306_draw_string(&ssd,"TELA DE ACESSO",5,10);
                ssd1306_draw_string(&ssd,"INFORME",35,25);
                ssd1306_draw_string(&ssd,"ID",55,40);
                ssd1306_send_data(&ssd);
                xSemaphoreGive(xMutexDisplay);// Libera o mutex do display para outra task
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}


// Task que exibe uma mensagem para apertar o botão A ou B e o id informado, enquanto nenhum deste dois botões for apertado
void vTaskInformaParaLogarOuDeslogar(void *params){
    char codigo;
    char stringCodigo[16];
    while (true){
        xSemaphoreTake(xSemaforoBinarioIDInformado,portMAX_DELAY);// Se um id for informado pelo teclado
        if (modo_display == ESPERANDO_BOTÃO){ // E o  modo do display for este
            if(xQueuePeek(xFilaCodigoIdentificacaoUsuario,&codigo,portMAX_DELAY) == pdTRUE){ // Capture o id e coloque na variável codigo
                snprintf(stringCodigo,sizeof(stringCodigo),"ID -> %c",codigo); // Para exibir no display
                xSemaphoreTake(xMutexDisplay,portMAX_DELAY); // Captura o mutex do display para usar o mesmo de forma segura
                // Limpa o display
                ssd1306_fill(&ssd, false);
                ssd1306_send_data(&ssd);
                // Exibe a mensagem
                ssd1306_rect(&ssd, 1,1,122,60, true, false);
                ssd1306_draw_string(&ssd,stringCodigo,30,10);
                ssd1306_draw_string(&ssd,"(A)-> LOGAR",5,25);
                ssd1306_draw_string(&ssd,"(B)-> DESLOGAR",5,45);
                ssd1306_send_data(&ssd);
                // Enquanto o botão A ou B não forem apertados a task continua exibindo esta mensagem
                xSemaphoreTake(xSemaforoBinarioProsseguirComLoginOuLogout, portMAX_DELAY);
                // Limpa o display
                ssd1306_fill(&ssd, false);
                ssd1306_send_data(&ssd);
                // Muda o estado do display
                modo_display = ESPERANDO_ID;
                xSemaphoreGive(xMutexDisplay);// Libera o display para outra task
            }
        }
    }
}


// Task que exibe uma mensagem de maximo de usuários cadastrados e emite um sinal sonoro
void vTaskAvisoLimite(void *params){
    while (true){
        xSemaphoreTake(xSemaforoBinarioAvisarLimiteUsuarios,portMAX_DELAY); // Se o limite for atingido
        if (xSemaphoreTake(xMutexDisplay,portMAX_DELAY) == pdTRUE){// Captura o mutex do display para usar o mesmo de fomra segura
            // Limpa o display
            ssd1306_fill(&ssd, false);
            ssd1306_send_data(&ssd);
            // Desenha o aviso
            ssd1306_rect(&ssd, 3,3,122,60, true, false);
            ssd1306_rect(&ssd, 4,4,120,58, true, false);
            ssd1306_draw_string(&ssd, "LIMITE DE", 32, 20);
            ssd1306_draw_string(&ssd, "USUARIOS!", 30, 35);
            ssd1306_send_data(&ssd);
            // Emite sinal com buzzer
            pwm_set_gpio_level(GPIO_PIN_BUZZER,WRAP/2);
            vTaskDelay(pdMS_TO_TICKS(1000));
            pwm_set_gpio_level(GPIO_PIN_BUZZER,0);
            // Limpa o display
            ssd1306_fill(&ssd,false);
            ssd1306_send_data(&ssd);
            xSemaphoreGive(xMutexDisplay);// Libera o display para outra task
        }
    }
    
}


// Task que controla a entrada de usuários no sistema acionado pelo Botão A
void vTaskEntradaUsuarios(void *params){
    // Configuraçãos gpios para botão e interrupçao acionada por ele
    gpio_init(BOTAO_A);
    gpio_set_dir(BOTAO_A,GPIO_IN);
    gpio_pull_up(BOTAO_A);
    gpio_set_irq_enabled_with_callback(BOTAO_A,GPIO_IRQ_EDGE_FALL,true,&gpio_irq_handler);
    char codigo;
    bool encontrado;
    while (true){
         if (xSemaphoreTake(xSemaforoBinarioAumentarUsuarios,pdMS_TO_TICKS(100)) == true){// Se o botão A foi apertado e o semáforo binário sinalizado
            encontrado = false;// Para verificar se o usuário com id digitado existe no sistema
            if ( xQueueReceive(xFilaCodigoIdentificacaoUsuario, &codigo, pdMS_TO_TICKS(100)) == pdTRUE ) {// Armazena o id digitado na variável código
                for (int i = 0; i < MAXIMO_USUARIOS; i++){// Itera sobre todos os usuários do sistema
                    if (usuarios[i].codigo == codigo && !usuarios[i].logado){// se o usuário for encontrado no sistema
                        encontrado = true;//Sinaliza que foi encontrado
                        usuarios[i].logado = true;// Muda o estado do usuário 
                        xSemaphoreGive(xSemaforoContagemUsuários); // Aumenta a contagem de usuários no sistema
                        if (xSemaphoreTake(xMutexDisplay,portMAX_DELAY) == pdTRUE){ // Tenta obter o mutex do display para usar com segurança
                            // Limpa o disaply
                            ssd1306_fill(&ssd, false);
                            ssd1306_send_data(&ssd);
                            // Exibe mensagem 
                            ssd1306_rect(&ssd, 1,1,122,60, true, false);
                            ssd1306_draw_string(&ssd, "USUARIO", 30, 10);
                            ssd1306_draw_string(&ssd, usuarios[i].nome, 30, 25);
                            ssd1306_draw_string(&ssd, "LOGADO", 30, 40);
                            ssd1306_send_data(&ssd);
                            vTaskDelay(pdMS_TO_TICKS(1000)); // Permanece no display por 1 segundo
                            // Limpa o display
                            ssd1306_fill(&ssd, false);
                            ssd1306_send_data(&ssd);
                            xSemaphoreGive(xMutexDisplay);// Libera o display para outras tasks
                        }
                        break;
                    }
                }
            }
            if (!encontrado && uxSemaphoreGetCount(xSemaforoContagemUsuários) < MAXIMO_USUARIOS){// Se o usuário não foi encontrado
                xSemaphoreTake(xMutexDisplay, portMAX_DELAY);// Tenta obter o mutex do display para usar com segurança
                // Limpa o display
                ssd1306_fill(&ssd, false);
                ssd1306_send_data(&ssd);
                // Exibe mensagem de que o usuário não foi encontrado
                ssd1306_rect(&ssd, 1,1,122,60, true, false);
                ssd1306_draw_string(&ssd,"USUARIO",35,10);
                ssd1306_draw_string(&ssd,"NAO",50,25);
                ssd1306_draw_string(&ssd,"ENCONTRADO",25,40);
                ssd1306_send_data(&ssd);
                vTaskDelay(pdMS_TO_TICKS(1000));// Permance no display por 1s
                ssd1306_fill(&ssd, false);
                ssd1306_send_data(&ssd);
                xSemaphoreGive(xMutexDisplay);// Libera o display para outras tasks
            }
            
            // Se o Sistema estiver cheio, envia sinal para semaforo binário alertar outra task
            if (uxSemaphoreGetCount(xSemaforoContagemUsuários) == MAXIMO_USUARIOS)xSemaphoreGive(xSemaforoBinarioAvisarLimiteUsuarios);
        }
    }
}



// Task que controla a saida de usuários no sistema ao botão B for pressionado
void vTaskSaidaUsuarios(void *params){
    // Configura os gpios para botão B e interrupção atrelada a ele
    gpio_init(BOTAO_B);
    gpio_set_dir(BOTAO_B,GPIO_IN);
    gpio_pull_up(BOTAO_B);
    gpio_set_irq_enabled_with_callback(BOTAO_B,GPIO_IRQ_EDGE_FALL,true,&gpio_irq_handler);
    char codigo;
    bool encontrado;
    while (true){
        if (xSemaphoreTake(xSemaforoBinarioDiminuirUsuarios,pdMS_TO_TICKS(100)) == pdTRUE){// Se o botão B foi apertado e o semáforo binário sinalizado
            encontrado = false;// Para verificar se o usuário com id informado foi encontrado no sistema
            if (xQueueReceive(xFilaCodigoIdentificacaoUsuario,&codigo,pdMS_TO_TICKS(100)) == pdTRUE){ // Armazena o id informado na variavel codigo
                for (int i = 0; i < MAXIMO_USUARIOS; i++){// Itera sobre todos os usuários
                    if (usuarios[i].codigo == codigo && usuarios[i].logado){// Se o usuário foi encontrado 
                        encontrado = true;// Sinaliza que foi encontrado
                        usuarios[i].logado = false;// Muda o estado do usuário
                        xSemaphoreTake(xSemaforoContagemUsuários,portMAX_DELAY); // remove do sistema
                        if (xSemaphoreTake(xMutexDisplay,pdMS_TO_TICKS(100)) == pdTRUE){ // Tenta obter o mutex do display para usar com segurança
                            // Limpa o display
                            ssd1306_fill(&ssd, false);
                            ssd1306_send_data(&ssd);
                            // Exibe mensagem de usuário deslogadao
                            ssd1306_rect(&ssd, 1,1,122,60, true, false);
                            ssd1306_draw_string(&ssd, "USUARIO", 30, 10);
                            ssd1306_draw_string(&ssd, usuarios[i].nome, 30, 25);
                            ssd1306_draw_string(&ssd, "DESLOGADO", 30, 40);
                            ssd1306_send_data(&ssd);
                            vTaskDelay(pdMS_TO_TICKS(1000));// Permanece no display por 1 s
                            ssd1306_fill(&ssd, false);
                            ssd1306_send_data(&ssd);
                            xSemaphoreGive(xMutexDisplay);// Libera o display para outra task
                        }
                        break;
                    }
                    
                }
                
            }
            if (!encontrado){ // Se o usuário não for encontrado
                xSemaphoreTake(xMutexDisplay, portMAX_DELAY);// Tenta obter o mutex do display para usar com segurança
                // Limpa o display
                ssd1306_fill(&ssd, false);
                ssd1306_send_data(&ssd);
                // Exibe mensagem que  o usuário não foi encontrado
                ssd1306_rect(&ssd, 1,1,122,60, true, false);
                ssd1306_draw_string(&ssd,"USUARIO",35,10);
                ssd1306_draw_string(&ssd,"NAO",50,25);
                ssd1306_draw_string(&ssd,"ENCONTRADO",25,40);
                ssd1306_send_data(&ssd);
                vTaskDelay(pdMS_TO_TICKS(1000));// Permanece no display por 1s
                ssd1306_fill(&ssd, false);
                ssd1306_send_data(&ssd);
                xSemaphoreGive(xMutexDisplay);// Libera o display para outra task
            }
        }
    }
}

// Task para resetar o sistema
void vTaskReset(void *params){
    // Configura gpio para botão e interrupção no joybtn
    gpio_init(BOTAO_JOYSTICK);
    gpio_set_dir(BOTAO_JOYSTICK,GPIO_IN);
    gpio_pull_up(BOTAO_JOYSTICK);
    gpio_set_irq_enabled_with_callback(BOTAO_JOYSTICK,GPIO_IRQ_EDGE_FALL,true,&gpio_irq_handler);

    while (true){
        xSemaphoreTake(xSemaforoBinarioResetSistema,portMAX_DELAY);// Se o botão do joystick foi apertado
        while (uxSemaphoreGetCount(xSemaforoContagemUsuários) > 0){ // Enquanto houver usuários no sistema
            xSemaphoreTake(xSemaforoContagemUsuários,portMAX_DELAY); // Retire 1 por 1
        }
        for (int i = 0; i < MAXIMO_USUARIOS; i++) usuarios[i].logado = false;// itera sobre todos o usuários e muda a tag de logado para deslogado
        // Emite dois beeps para sinalizar que sistema resetou
        for (int i = 0; i < 2; i++){
            pwm_set_gpio_level(GPIO_PIN_BUZZER,WRAP/2);
            vTaskDelay(pdMS_TO_TICKS(100));
            pwm_set_gpio_level(GPIO_PIN_BUZZER,0);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}



int main(){
    stdio_init_all();


    // Configurações necessárias para controle do pwm
    gpio_set_function(GPIO_PIN_BUZZER, GPIO_FUNC_PWM);
    uint slice_num = pwm_gpio_to_slice_num(GPIO_PIN_BUZZER);
    pwm_config config = pwm_get_default_config();
    pwm_set_wrap(slice_num, WRAP - 1);
    pwm_config_set_clkdiv(&config, clock_get_hz(clk_sys)/(850 * WRAP)); 
    pwm_init(slice_num, &config, true);
    pwm_set_gpio_level(GPIO_PIN_BUZZER, 0);

     // Inicializa a conexao i2c e gpios necessárias
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    // Inicializa o display oled sem nada desenhado
    ssd1306_init(&ssd, WIDTH, HEIGHT, false, endereco, I2C_PORT);
    ssd1306_config(&ssd);
    ssd1306_fill(&ssd,false);
    ssd1306_send_data(&ssd);

    

    // Mutex usado
    xMutexDisplay = xSemaphoreCreateMutex();
    // Semáforos binários usados
    xSemaforoBinarioResetSistema = xSemaphoreCreateBinary();
    xSemaforoBinarioAumentarUsuarios = xSemaphoreCreateBinary();
    xSemaforoBinarioDiminuirUsuarios = xSemaphoreCreateBinary();
    xSemaforoBinarioAvisarLimiteUsuarios = xSemaphoreCreateBinary();
    xSemaforoBinarioIDInformado = xSemaphoreCreateBinary();
    xSemaforoBinarioProsseguirComLoginOuLogout = xSemaphoreCreateBinary();
    // Semáforo de Contagem
    xSemaforoContagemUsuários = xSemaphoreCreateCounting(MAXIMO_USUARIOS,0);
    //Fila
    xFilaCodigoIdentificacaoUsuario = xQueueCreate(1,sizeof (char));
    // Criação de task
    xTaskCreate(vTaskReset,"Reseta o sistema",configMINIMAL_STACK_SIZE,NULL,tskIDLE_PRIORITY + 1,NULL);
    xTaskCreate(vTaskEntradaUsuarios,"Adiciona um usuário",configMINIMAL_STACK_SIZE,NULL,tskIDLE_PRIORITY + 1,NULL);
    xTaskCreate(vTaskSaidaUsuarios,"Retira um usuário",configMINIMAL_STACK_SIZE,NULL,tskIDLE_PRIORITY + 1,NULL);
    xTaskCreate(vTaskEstadoSistemaComRgb,"Mostra o estado do sistema com leds rgb",configMINIMAL_STACK_SIZE,NULL,tskIDLE_PRIORITY + 1,NULL);
    xTaskCreate(vTaskMostraMensagemDeEsperaPorID,"Mostra mensagem de espera por ID",configMINIMAL_STACK_SIZE,NULL,tskIDLE_PRIORITY + 1,NULL);
    xTaskCreate(vTaskAvisoLimite,"Emite um aviso que o máximo de usuário foi atingido",configMINIMAL_STACK_SIZE,NULL,tskIDLE_PRIORITY + 1,NULL);
    xTaskCreate(vMatrizLedTask,"Controla a matriz de leds",configMINIMAL_STACK_SIZE,NULL,tskIDLE_PRIORITY + 1,NULL);
    xTaskCreate(vTaskUART,"Lé os id informados pelo teclado",configMINIMAL_STACK_SIZE,NULL,tskIDLE_PRIORITY + 1,NULL);
    xTaskCreate(vTaskInformaParaLogarOuDeslogar,"Mostra mensagem para logar ou deslogar usuário",configMINIMAL_STACK_SIZE,NULL,tskIDLE_PRIORITY + 1,NULL);
    // Inicia o agendador
    vTaskStartScheduler();
    panic_unsupported();
    
}
