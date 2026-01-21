#!/bin/bash
# ==============================================================================
# Development Reload Script
# ==============================================================================
# Quickly reload the servers after making code changes.
#
# Usage:
#   ./dev-reload.sh           # Reload both servers
#   ./dev-reload.sh admin     # Reload admin server only
#   ./dev-reload.sh api       # Reload subscription server only
#
# Note: This requires running with docker-compose.dev.yml
#   docker-compose -f docker-compose.yml -f docker-compose.dev.yml up -d
#
# ==============================================================================

CONTAINER="owl-license-server"

reload_admin() {
    echo "Reloading admin server..."
    docker exec $CONTAINER supervisorctl restart admin-server
}

reload_api() {
    echo "Reloading subscription server..."
    docker exec $CONTAINER supervisorctl restart subscription-server
}

case "$1" in
    admin)
        reload_admin
        ;;
    api|subscription)
        reload_api
        ;;
    status)
        echo "Server status:"
        docker exec $CONTAINER supervisorctl status
        ;;
    logs)
        echo "Following logs..."
        docker logs -f $CONTAINER
        ;;
    *)
        echo "Reloading all servers..."
        docker exec $CONTAINER supervisorctl restart owl-license-server:
        ;;
esac

echo ""
echo "Done! Server status:"
docker exec $CONTAINER supervisorctl status
