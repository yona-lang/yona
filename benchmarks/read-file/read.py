import aiofiles
import asyncio
import time

wc = 0


async def run():
    global wc
    async with aiofiles.open('../data/big.txt') as f:
        async for line in f:
            wc += 1


def main():
    loop = asyncio.get_event_loop()
    start_time = time.perf_counter()
    loop.run_until_complete(run())
    print(((time.perf_counter() - start_time) * 1000 * 1000, wc))
    loop.close()


if __name__ == '__main__':
    main()
