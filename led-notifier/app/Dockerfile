FROM node:22-alpine

RUN apk add --no-cache ca-certificates

WORKDIR /app

COPY package.json ./
RUN npm install --production

COPY . .

ENV NODE_ENV=production
EXPOSE 3000

CMD ["npm", "start"]
