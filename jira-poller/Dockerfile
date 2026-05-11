FROM oven/bun:1.2-alpine

WORKDIR /app

COPY package.json bun.lock ./
RUN bun install --frozen-lockfile --production

COPY poller.ts tsconfig.json ./

ENV NODE_ENV=production

CMD ["bun", "run", "poller.ts"]
