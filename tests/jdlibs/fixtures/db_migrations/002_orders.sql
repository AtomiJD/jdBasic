CREATE TABLE orders (
    id INTEGER PRIMARY KEY,
    customer_id INTEGER NOT NULL REFERENCES customers(id),
    amount REAL NOT NULL
);
CREATE INDEX orders_by_customer ON orders (customer_id)
-- the last statement has no semicolon and ends in a comment
