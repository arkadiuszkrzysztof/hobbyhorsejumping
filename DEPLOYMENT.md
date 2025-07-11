# Deployment Options

## Option 1: Local Development (Recommended for testing)
Use the provided startup scripts:
- `setup-and-start.bat` - Simple batch file setup
- `startup-script.ps1` - Advanced PowerShell setup with auto-start options

## Option 2: Docker Deployment (Advanced users)

### Prerequisites
- Docker Desktop for Windows
- Docker Compose

### Quick Start with Docker

1. Build and start services:
```bash
docker-compose up --build
```

2. Access the services:
- Backend: http://localhost:8000
- Frontend: http://localhost:3000

### Docker Configuration

#### Backend Dockerfile
```dockerfile
FROM python:3.12-slim

WORKDIR /app
COPY requirements.txt .
RUN pip install -r requirements.txt

COPY . .
RUN python manage.py migrate

EXPOSE 8000
CMD ["python", "manage.py", "runserver", "0.0.0.0:8000"]
```

#### Frontend Dockerfile
```dockerfile
FROM node:18-alpine

WORKDIR /app
COPY package*.json ./
RUN npm install

COPY . .
RUN npm run build

EXPOSE 3000
CMD ["npm", "start"]
```

#### Docker Compose
```yaml
version: '3.8'

services:
  backend:
    build: ./hhj-backend
    ports:
      - "8000:8000"
    environment:
      - DEBUG=1
    volumes:
      - ./hhj-backend:/app
    
  frontend:
    build: ./hhj-frontend
    ports:
      - "3000:3000"
    environment:
      - REACT_APP_API_HTTP_URL=http://localhost:8000
      - REACT_APP_API_WEBSOCKET_URL=ws://localhost:8000
    volumes:
      - ./hhj-frontend:/app
    depends_on:
      - backend

  redis:
    image: redis:alpine
    ports:
      - "6379:6379"
```

### Production Deployment Notes

For production use, consider:

1. **Database**: Replace SQLite with PostgreSQL
2. **WebSocket Backend**: Use Redis instead of in-memory channels
3. **Web Server**: Use nginx + gunicorn for backend
4. **Security**: Configure proper CORS, CSRF, and SSL
5. **Monitoring**: Add logging and health checks
6. **Scaling**: Use multiple backend instances if needed

### Environment Variables

Create `.env` files for configuration:

#### Backend `.env`
```
DEBUG=False
SECRET_KEY=your-secret-key-here
DATABASE_URL=postgresql://user:pass@localhost/hhj
REDIS_URL=redis://localhost:6379/0
ALLOWED_HOSTS=localhost,127.0.0.1,your-domain.com
```

#### Frontend `.env`
```
REACT_APP_API_HTTP_URL=https://your-domain.com/api
REACT_APP_API_WEBSOCKET_URL=wss://your-domain.com/ws
```
