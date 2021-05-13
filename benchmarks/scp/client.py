import aiofiles
import asyncio
import time


async def client():
    reader, writer = await asyncio.open_connection('127.0.0.1', 5555)

    async with aiofiles.open('../data/big.txt') as f:
        async for line in f:
            writer.write(line.encode('utf-8'))

    writer.write(b'--over--\n')
    await writer.drain()

    writer.close()
    await writer.wait_closed()


start_time = time.perf_counter()
asyncio.run(client())
print((time.perf_counter() - start_time) * 1000 * 1000)
