package com.example.lorabridge.domain.model

/**
 * Sealed class for handling success/failure results
 */
sealed class Result<out T> {
    data class Success<T>(val data: T) : Result<T>()
    data class Error(val exception: Throwable, val message: String? = null) : Result<Nothing>()

    fun isSuccess(): Boolean = this is Success
    fun isError(): Boolean = this is Error

    fun getOrNull(): T? = when (this) {
        is Success -> data
        is Error -> null
    }

    fun getOrThrow(): T = when (this) {
        is Success -> data
        is Error -> throw exception
    }

    inline fun onSuccess(action: (T) -> Unit): Result<T> {
        if (this is Success) action(data)
        return this
    }

    inline fun onError(action: (Throwable) -> Unit): Result<T> {
        if (this is Error) action(exception)
        return this
    }
}

/**
 * Helper to create Success result
 */
fun <T> success(data: T): Result<T> = Result.Success(data)

/**
 * Helper to create Error result
 */
fun error(exception: Throwable, message: String? = null): Result<Nothing> =
    Result.Error(exception, message)
