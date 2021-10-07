FROM node:16-buster

ARG PORT=3000
ENV PORT $PORT
EXPOSE $PORT

ARG NODE_ENV=production
ENV NODE_ENV $NODE_ENV

WORKDIR /app
COPY . /app

RUN npm install

USER node
ENTRYPOINT [ "/app/entrypoint.sh" ]
