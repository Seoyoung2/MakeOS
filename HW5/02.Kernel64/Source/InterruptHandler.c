#include "InterruptHandler.h"
#include "PIC.h"
#include "Keyboard.h"
#include "Console.h"
#include "Utility.h"
#include "Task.h"
#include "Descriptor.h"
#include "ConsoleShell.h"
#include "ScreenSaver.h"
#include "HardDisk.h"

static inline void invlpg(void* m){
    asm volatile("invlpg (%0)" ::"b"(m) : "memory");
}

// 페이지 폴트 출력
void kPrintPageFault(QWORD qwExceptionAddress){
    QWORD test = 15;
    kPrintf("=========================================\n");
    kPrintf("           Page Fault Occur~!!!!         \n");
    kPrintf("           Address: 0x%q\n", qwExceptionAddress);
    kPrintf("=========================================\n");
}

// protection fault 출력
void kPrintProtectionFault(QWORD qwExceptionAddress){
    kPrintf("=========================================\n");
    kPrintf("        Protection Fault Occur~!!!!      \n");
    kPrintf("           Address: 0x%q\n", qwExceptionAddress);
    kPrintf("=========================================\n");
}

void kProcessFault(QWORD qwExceptionAddress, BOOL bProtectionFault){
    DWORD dwOffset, dwTableOffset, dwDirectoryOffset, dwDirPointerOffset, dwPML4Offset;
    QWORD *qwTableEntry, *qwDirectoryEntry, *qwDirPointerEntry, *qwPML4Entry;

    // 예외가 발생한 선형 주소를 5단계로 나눔
    dwOffset = qwExceptionAddress & MASK_OFFSET;
    dwTableOffset = (qwExceptionAddress >> SHIFT_TABLE) & MASK_ENTRYOFFSET;
    dwDirectoryOffset = (qwExceptionAddress >> SHIFT_DIRECTORY) & MASK_ENTRYOFFSET;
    dwDirPointerOffset = (qwExceptionAddress >> SHIFT_DIRECTORYPTR) & MASK_ENTRYOFFSET;
    dwPML4Offset = (qwExceptionAddress >> SHIFT_PML4) & MASK_ENTRYOFFSET;

    // 예외가 발생한 선형 주소의 페이지 테이블 엔트리를 찾음
    qwPML4Entry = kGetPML4BaseAddress() + SIZE_ENTRY * dwPML4Offset;
    qwDirPointerEntry = (*qwPML4Entry & MASK_BASEADDRESS) + SIZE_ENTRY * dwDirPointerOffset;
    qwDirectoryEntry = (*qwDirPointerEntry & MASK_BASEADDRESS) + SIZE_ENTRY * dwDirectoryOffset;
    qwTableEntry = (*qwDirectoryEntry & MASK_BASEADDRESS) + SIZE_ENTRY * dwTableOffset;

    // protection fault 처리
    if(bProtectionFault){
        *qwTableEntry |= 2;
        invlpg(qwExceptionAddress);
        kPrintProtectionFault(qwExceptionAddress);
    }
    // present bit 1로 set
    else{
        *qwTableEntry |= 1;
        invlpg(qwExceptionAddress);
        kPrintPageFault(qwExceptionAddress);
    }
}

void kCommonExceptionHandler(int iVectorNumber, QWORD qwErrorCode){
    char vcBuffer[3] = {0, };

    // 인터럽트 벡터를 화면 오른쪽 위에 2자리 정수로 출력
    vcBuffer[0] = '0' + iVectorNumber / 10;
    vcBuffer[1] = '0' + iVectorNumber % 10;

    kPrintf("=========================================\n");
    kPrintf("            Exception Occur~!!!!         \n");
    kPrintf("                 Vector: %d \n", kAToI(vcBuffer, 10));
    kPrintf("=========================================\n");

    while(1);
}

void kPageFaultHandler(int iVectorNumber, QWORD qwErrorCode){
    // 예외가 발생한 주소
    QWORD qwExceptionAddress = kGetExceptionAddress();
    // 다시 쉘로 복귀하기 위해 예외를 처리
    kProcessFault(qwExceptionAddress, qwErrorCode & 1);
}

void kCommonInterruptHandler(int iVectorNumber){
    char vcBuffer[] = "[INT:  , ]";
    static int g_iCommonInterruptCount = 0;

    //===============================================================
    // 인터럽트가 발생했음을 알리고 메시지를 출력
    vcBuffer[5] = '0' + iVectorNumber / 10;
    vcBuffer[6] = '0' + iVectorNumber % 10;
    // 발생한 횟수 출력
    vcBuffer[8] = '0' + g_iCommonInterruptCount;
    g_iCommonInterruptCount = (g_iCommonInterruptCount + 1) % 10;
    //kPrintStringXY(70, 0, vcBuffer);
    //===============================================================

    // EOI 전송
    kSendEOIToPIC(iVectorNumber - PIC_IRQSTARTVECTOR);
}

void kKeyboardHandler(int iVectorNumber){
    char vcBuffer[] = "[INT:  , ]";
    static int g_iKeyboardInterruptCount = 0;
    BYTE bTemp;

    // 화면보호기가 실행중이었다면 화면보호기 종료
    if(g_stScreenSaver.bScreenSaverOn){
        kScreenSaverOff();
    }
    // 인터럽트가 발생하고 지난 시간을 0으로 초기화
    g_stScreenSaver.qwScreenOnCount = 0;

    //===============================================================
    // 인터럽트가 발생했음을 알리고 메시지를 출력
    vcBuffer[5] = '0' + iVectorNumber / 10;
    vcBuffer[6] = '0' + iVectorNumber % 10;
    // 발생한 횟수 출력
    vcBuffer[8] = '0' + g_iKeyboardInterruptCount;
    g_iKeyboardInterruptCount = (g_iKeyboardInterruptCount + 1) % 10;
    //kPrintStringXY(0, 0, vcBuffer);
    //===============================================================

    // 키보드 컨트롤러에서 데이터를 읽어서 아스키로 변환하여 큐에 삽입
    if(kIsOutputBufferFull() == TRUE){
        bTemp = kGetKeyboardScanCode();
        kConvertScanCodeAndPutQueue(bTemp);
    }

    // EOI 전송
    kSendEOIToPIC(iVectorNumber - PIC_IRQSTARTVECTOR);
}

void kTimerHandler(int iVectorNumber){
    char vcBuffer[] = "[INT:  , ]";
    static int g_iTimerInterruptCounter = 0;

    //===============================================================
    // 인터럽트가 발생했음을 알리고 메시지를 출력
    vcBuffer[5] = '0' + iVectorNumber / 10;
    vcBuffer[6] = '0' + iVectorNumber % 10;
    // 발생한 횟수 출력
    vcBuffer[8] = '0' + g_iTimerInterruptCounter;
    g_iTimerInterruptCounter = (g_iTimerInterruptCounter + 1) % 10;
    //kPrintStringXY(70, 0, vcBuffer);
    //===============================================================

    // EOI 전송
    kSendEOIToPIC(iVectorNumber - PIC_IRQSTARTVECTOR);
    // 타이머 발생 횟수를 증가
    g_qwTickCount++;
    g_stScreenSaver.qwScreenOnCount++;

    // 태스크가 사용한 프로세서의 시간을 줄임
    kDecreaseProcessorTime();
    // 프로세서가 사용할 수 있는 시간을 다 썼다면 태스크 전환 수행
    if(kIsProcessorTimeExpired() == TRUE){
        kScheduleInInterrupt();
    }

    // 화면 보호기 실행
    if(g_stScreenSaver.qwScreenOnCount == g_stScreenSaver.qwRestrictionScreenOn && !g_stScreenSaver.bScreenSaverOn){
        kScreenSaverOn();
    }

    // 0.5초 단위로 태스크 정보 출력(사용한 프로세서 시간을 확인하기 위함)
    // if(g_qwTickCount % 100 == 0){
    //     kCallCls();
    //     kCallTaskList();
    // }
}

/**
 *  하드 디스크에서 발생하는 인터럽트의 핸들러
 */
void kHDDHandler( int iVectorNumber )
{
    char vcBuffer[] = "[INT:  , ]";
    static int g_iHDDInterruptCount = 0;
    BYTE bTemp;

    //=========================================================================
    // 인터럽트가 발생했음을 알리려고 메시지를 출력하는 부분
    // 인터럽트 벡터를 화면 왼쪽 위에 2자리 정수로 출력
    vcBuffer[ 5 ] = '0' + iVectorNumber / 10;
    vcBuffer[ 6 ] = '0' + iVectorNumber % 10;
    // 발생한 횟수 출력
    vcBuffer[ 8 ] = '0' + g_iHDDInterruptCount;
    g_iHDDInterruptCount = ( g_iHDDInterruptCount + 1 ) % 10;
    // 왼쪽 위에 있는 메시지와 겹치지 않도록 (10, 0)에 출력
    //kPrintStringXY( 10, 0, vcBuffer );
    //=========================================================================

    // 첫 번째 PATA 포트의 인터럽트 벡터(IRQ 14) 처리
    if( iVectorNumber - PIC_IRQSTARTVECTOR == 14 )
    {
        // 첫 번째 PATA 포트의 인터럽트 발생 여부를 TRUE로 설정
        kSetHDDInterruptFlag( TRUE, TRUE );
    }
    // 두 번째 PATA 포트의 인터럽트 벡터(IRQ 15) 처리
    else
    {
        // 두 번째 PATA 포트의 인터럽트 발생 여부를 TRUE로 설정
        kSetHDDInterruptFlag( FALSE, TRUE );
    }
    
    // EOI 전송
    kSendEOIToPIC( iVectorNumber - PIC_IRQSTARTVECTOR );
}