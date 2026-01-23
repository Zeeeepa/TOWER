"""
SQLAlchemy declarative base for all database models.

This module is separated to avoid circular imports between database.py and service_models.py.
"""

from sqlalchemy.orm import DeclarativeBase


class Base(DeclarativeBase):
    """SQLAlchemy declarative base for all models."""

    pass
