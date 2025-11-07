/// Domain layer - core business logic independent of infrastructure.
/// This layer contains the application's business rules and use cases.
/// It depends only on the ports (traits), not on concrete implementations.
pub mod message_router;
