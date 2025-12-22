#ifndef BOARD_TYPE_H
#define BOARD_TYPE_H

#define BOARD_VERSION "V0.0.2"

 #define BOARD_TYPE_BASE_UNICORE // UM982 base
// #define BOARD_TYPE_BASE_UBLOX // f9p base
//  #define BOARD_TYPE_ROVER_UNICORE // um982 rover
//   #define BOARD_TYPE_ROVER_UBLOX // f9p rover

#if defined(BOARD_TYPE_BASE_UNICORE) + defined(BOARD_TYPE_BASE_UBLOX) +         \
        defined(BOARD_TYPE_ROVER_UNICORE) + defined(BOARD_TYPE_ROVER_UBLOX) != 1 \

#error " 보드 타입을 하나만 설정해야 합니다"
#endif

#endif
