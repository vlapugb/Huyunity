#pragma once

#include <expected>

namespace huynity::objectlib {

enum class CreateErrorCode {
    NonFiniteParameter,
    InvalidRadius,
    InvalidHeight,
    TooFewVertices,
    InvalidDirection,
    InvalidDimensions,
    InvalidBounds,
    InvalidIndex,
    InvalidDescription,
    SelfIntersection,
    DegenerateGeometry,
    NumericalFailure,
};

struct CreateError {
    CreateErrorCode code;
    friend bool operator==(const CreateError&, const CreateError&) = default;
};

enum class GeometryErrorCode {
    InvalidOptions,
    BudgetExceeded,
    NumericalFailure,
    UnsupportedOperation,
    EmptyGeometry,
    InvalidGeometry,
};

struct GeometryError {
    GeometryErrorCode code;
    friend bool operator==(const GeometryError&, const GeometryError&) = default;
};

template <typename T>
using CreateResult = std::expected<T, CreateError>;

template <typename T>
using GeometryResult = std::expected<T, GeometryError>;

} // namespace huynity::objectlib
