#!/usr/bin/env python3
"""
Real-time audio listener and plotting application.
Qt GUI + Matplotlib + UDP receiver + AFSK string decoder.
"""

import sys
import asyncio
from graphic import main

if __name__ == '__main__':
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("Program interrupted by user")
    except Exception as e:
        print(f"Program error: {e}")
        sys.exit(1)
