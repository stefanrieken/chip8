#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <termios.h>
#include <poll.h>

// 4096 = 0x1000
#define memsize 4096
unsigned char memory[memsize] = {
  // The first 512 chars of memory are reserved.
  // One common use is to store the sprites for numbers 0-F
  // Apparenly start address 0x50 is traditional, but as
  // any adress will do, we just use address 0 so that we
  // can quickly express it in C like below.
  0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
  0x20, 0x60, 0x20, 0x20, 0x70, // 1
  0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
  0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
  0x90, 0x90, 0xF0, 0x10, 0x10, // 4
  0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
  0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
  0xF0, 0x10, 0x20, 0x40, 0x40, // 7
  0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
  0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
  0xF0, 0x90, 0xF0, 0x90, 0x90, // A
  0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
  0xF0, 0x80, 0x80, 0x80, 0xF0, // C
  0xE0, 0x90, 0x90, 0x90, 0xE0, // D
  0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
  0xF0, 0x80, 0xF0, 0x80, 0x80  // F
};
#define displayStart (4096-256)
#define displayWidth 64
#define displayWidthBytes 8
#define displayHeight 32


unsigned char reg[16];
uint16_t address;
int pc = 0x200; // Start at address 512
int sp = 0xEA0; // Place stack just before memory
int delayTimer = 0;
int soundTimer = 0;
bool screenChanged = true;

void reset() {
  pc = 0x200; // Start at address 512
  sp = 0xEA0; // Place stack just before memory
  delayTimer = 0;
  soundTimer = 0;
  screenChanged = true;
}

void draw_sprite(int x, int y, int height) {
  reg[0xF] = 0; // Here we mark collision detection
  
  x &= 0x3F; // 'wrap' values over 63
  y &= 0x1F; // 'wrap' values over 31

  int xByte  = x / 8;
  int offset = x % 8;

  // Translate from sprite width = 1 byte to display width = 8 bytes
  for(int i=0;i<height;i++) {
    int src  = address + i;
    int dest = displayStart + ((y+i) * displayWidthBytes) + xByte;

    if(dest > 0xFFF) break; // Don't go outside of (screen) memory

    reg[0xF] |= (memory[dest] & (memory[src] >> offset)) != 0;
    memory[dest] ^= memory[src] >> offset;
    if (x+1 < 64) {
      reg[0xF] |= (memory[dest+1] & (memory[src] << (8-offset))) != 0;
      memory[dest+1] ^= memory[src] << (8-offset);
    }
  }
  screenChanged = true;
}

void draw_screen() {
  if (!screenChanged) return;

  printf("\e[1;1H"); // Go to top left

  for(int y=0;y<displayHeight;y++) {
    for (int x=0;x<displayWidthBytes;x++) {
      char val = memory[displayStart+(y*8)+x];
      for(int i=0;i<8;i++) {
        printf("%s", (val & (1<<(7-i))) ? "\e[7m \e[0m" : " ");
      }
    }
    printf("\n");
  }
  screenChanged = false;
}

void math_op(int vx, int vy, int op)
{
  switch(op)
  {
    case 0x0:
      reg[vx] = reg[vy];
      break;
    case 0x1:
      reg[vx] |= reg[vy];
      break;
    case 0x2:
      reg[vx] &= reg[vy];
      break;
    case 0x3:
      reg[vx] ^= reg[vy];
      break;
    case 0x4:
      reg[vx] += reg[vy];
      break;
    case 0x5:
      reg[vx] -= reg[vy];
      break;
    case 0x6:
      reg[vx] >>= reg[vy];
      break;
    case 0x7:
      reg[vx] = reg[vy] - reg[vx];
      break;
    case 0xE:
      reg[vx] <<= reg[vy];
      break;
  }
}

void set_bcd(int value)
{
  address = value % 10;
  address |= ((value / 10) % 10) << 4;
  address |= ((value / 100) % 10) << 8;
}

char scan_key(bool wait) {
  if (!wait) {
    struct pollfd p;
    p.fd = 0; p.events = POLLIN;
    if (poll(&p, 1, 3500) <= 0) return 0xFF;
  }

  while (wait) {
    int ch = getchar();
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return (ch - 'a') + 10;
    if (ch >= 'A' && ch <= 'F') return (ch - 'A') + 10;
  }

  return 0xFF; // the equivalent of '-1'
}

void run1(uint16_t opcode)
{
  uint8_t op = (opcode >> 12) & 0xF;
  uint8_t vx = (opcode >> 8) & 0xF;
  uint8_t vy = (opcode >> 4) & 0xF;
  uint8_t arg = opcode & 0xFF;

//  printf("opcode: %x op: %x vx: %d vy: %d\n", opcode, op, vx, vy);

  switch(op)
  {
    case 0x0:
      if(opcode == 0x00E0) for(int i=displayStart;i<memsize;i++) memory[i]=0; // clear screen
      if(opcode == 0x00EE) { sp -= 2; pc = (memory[sp]<<8) | memory[sp+1]; if (sp < 0xEA0) printf("Stack underflow!\n"); } // return from subroutine
      break;
    case 0x1:
      pc = opcode & 0xFFF; // jump to address
      break;
    case 0x2:
      memory[sp] = pc >> 8; sp++; memory[sp] = pc & 0xFF; sp++; pc = opcode & 0xFFF;  // jump to subroutine
      break;
    case 0x3:
      if (reg[vx] == (opcode & 0xFF)) pc+=2; // skip if equal
      break;
    case 0x4:
      if (reg[vx] != (opcode & 0xFF)) pc+=2; // skip if not equal
      break;
    case 0x5:
      if (reg[vx] == reg[vy]) pc+=2; // skip if equal; NOTE arg not used (could be offset)
      break;
    case 0x6:
      reg[vx] = opcode & 0xFF;
      break;
    case 0x7:
      reg[vx] += opcode & 0xFF;
      break;
    case 0x8:
      math_op(vx, vy, opcode&0xF);
      break;
    case 0x9:
      if (reg[vx] != reg[vy]) pc+=2; // skip if not equal; NOTE arg not used (could be offset)
      break;
    case 0xA:
      address = opcode & 0xFFF;
      break;
    case 0xB:
      pc = (opcode & 0xFFF) + reg[0]; // jump to address
      break;
    case 0xC:
      memory[vx] = rand() & arg; // generate random (small) int
      break;
    case 0xD:
      draw_sprite(reg[vx], reg[vy], opcode&0xF);
      break;
    case 0xE:
      if (arg == 0x9E && scan_key(false) == memory[vx] && memory[vx] != 0xFF) pc += 2;
      if (arg == 0xA1 && scan_key(false) != memory[vx] && memory[vx] != 0xFF) pc += 2;
      // TODO keys
      break;
    case 0xF:
      if(arg == 0x07) reg[vx] = delayTimer; 
      if(arg == 0x0A) reg[vx] = scan_key(true);
      if(arg == 0x15) delayTimer = reg[vx]; 
      if(arg == 0x18) soundTimer = reg[vx]; 
      if(arg == 0x1E) address += reg[vx];
      if(arg == 0x29) address = reg[vx]*5; // save address of character sprite 
      if(arg == 0x33) set_bcd(reg[vx]);
      if(arg == 0x55) for(int i=0;i<=vx;i++) memory[address+i] = reg[i];
      if(arg == 0x65) for(int i=0;i<=vx;i++) reg[i] = memory[address+i];
      break;
  }
}

void run()
{
  reset();
  unsigned int counter = 0;
  int opcode;
  opcode = memory[pc] << 8; pc++;
  opcode |= memory[pc]; pc++;

  draw_screen();
  
  // It is not stated anywhere that zero means stop,
  // but as it doesn't mean anything else, now it does.
  while(opcode != 0) {
    run1(opcode);
    
    if (counter % 8 == 0) {
      draw_screen();
      if(delayTimer > 0) delayTimer--;
      if(soundTimer > 0) soundTimer--;
    }
    // The vague timing spec is 500 Hz,
    // but the screen refresh rate is 60.
    // Settle at sleeping 1/500th of a second
    // independent of execution time,
    // and refresh screen every 8 cycles.
    usleep(2000);

    opcode = memory[pc] << 8; pc++;
    opcode |= memory[pc]; pc++;
  }
}

static struct termios oldflags, newflags;
void set_input_unbuffered_no_echo() {
  tcgetattr(STDIN_FILENO, &oldflags);
  newflags = oldflags;
  newflags.c_lflag &= ~(ICANON|ECHO);
  newflags.c_cc[VMIN]=1;
  newflags.c_cc[VTIME]=0;
  tcsetattr(STDIN_FILENO, TCSANOW, &newflags);
}
void reset_terminal() {
//  oldflags.c_lflag |= ECHO|ICANON;
  tcsetattr(STDIN_FILENO, TCSANOW, &oldflags);
}

void save_rom(char * filename) {
  FILE * file = fopen(filename, "wb");
  for(int i=512;i<pc;i++) {
    fputc(memory[i], file);
  }
  fclose(file);
}

#define oper(a,b) memory[pc++]=a;memory[pc++]=b

int main (int argc, char ** argv)
{
  set_input_unbuffered_no_echo();

  printf("\e[2J"); // Clear screen
/*
  // Do a test / demo run
  for (int j=0; j<5;j++) {
    for (int i=0; i<16;i++) {
//      address = i*5;
      run1(0x6300 | i); // Set reg3 to sprite idx
      run1(0xF329); // Load address of sprite idx

      run1(0x6100 | (j+i*8));     // set reg1 to x coord
      run1(0x6200 | (j+(i/8)*6)); // set reg2 to y coord
      run1(0xD125); // draw sprite
    }
    draw_screen();
    run1(0x00E0); // clear screen

    usleep(100000);
  }

  // Now do the above as a program
  reset();

  oper(0x64, 0x00); // Set v4 (loop counter) to 0
  uint16_t loop1 = pc;
  // clear screen
  oper(0x00,0xE0);
  
  // Still have to work out this loop in target code
  for (int i=0; i<16;i++) {
    oper(0x63,i);      // set reg v3 to i
    oper(0xF3,0x29);   // load sprite for num i into address register
    oper(0x61, i*8);   // set reg v1 to x coord
    oper(0x81, 0x44);  // add offset counter v4
    oper(0x62, (i/8)*6); // set reg v2 to y coord
    oper(0x82, 0x44);  // add offset counter v4
    oper(0xD1,0x25);  // Draw sprite at x, y, 5 high
  }

  oper(0x63, 60);   // set reg v3 to 60 counts
  oper(0xF3, 0x15); // set timer

  uint16_t loop2 = pc;
  oper(0xF3,0x07); // read timer into v3
  oper(0x33,0x00); // Is v3 zero? then don't...
  // ...loop
  oper(0x10 | loop2>>8,loop2 & 0xFF);

  oper(0x74,0x01); // v4 += 1
  oper(0x34,0x05); // Is v4 5 then don't...
  // ...loop
  oper(0x10 | loop1>>8,loop1 & 0xFF);
  oper(0,0);

  run();
*/

  // Test program: scan for input and display it on screen
  oper(0xF1, 0x0A); // scan key in reg1
  oper(0xF1, 0x29); // store its address
  oper(0x60, 0x00); // Set reg 0 to zero
  oper(0xD0, 05);   // and print sprite on 0,0
  save_rom("show_keypress.ch8");
  run();

/*
  reset();
//  FILE * file = fopen("game.c8", "rb");
  FILE * file = fopen("test_opcode.ch8", "rb");
  int ch = getc(file);
  while(ch != EOF) {
    memory[pc++] = ch;
    ch = getc(file);
  }
  fclose(file);

  run();
 */ 
  reset_terminal();
}
